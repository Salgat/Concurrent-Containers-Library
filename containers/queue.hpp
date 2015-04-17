//
// Created by Austin Salgat on 4/1/2015.
//

#ifndef CCL_QUEUE_HPP
#define CCL_QUEUE_HPP

#include <utility>
#include <memory>
#include <atomic>
#include <iostream>
#include <thread>

namespace ccl {
    /**
     * Concurrent queue. A simplified version of std::queue that allows for concurrent access. Implemented using
     * flat combining, outlined here: http://www.cs.bgu.ac.il/~hendlerd/papers/flat-combining.pdf
     */
    template<typename T>
    class queue {
    private:
        struct node {
            node* next;
            T data;

            node(T data_)
                    : data(data_)
                    , next(nullptr) { }
        };

        enum class RequestType {
            PUSH,
            POP,
            RESPONSE_PUSH,
            RESPONSE_POP,
            RESPONSE_POP_FAIL,
            NULL_RESPONSE
        };

        struct publication_record {
            publication_record* next;
            std::pair<RequestType, T> request;
            unsigned int age;
            std::atomic<bool> active;
        };

        node* head;
        node* tail;

        std::atomic<publication_record*> publication_head;
        unsigned int combining_pass_counter;
        std::atomic_flag combiner_lock;

        /**
         * Handles pending thread requests.
         */
        void combiner() {
            ++combining_pass_counter;

            // Traverse publication list from the head, updating age of non-null records and and processing requests
            auto current_record = publication_head.load();
            publication_record* previous_record = nullptr;
            while (current_record) {
                if (current_record->request.first != RequestType::NULL_RESPONSE) {
                    // Update the age of all non-null requests and apply the methods they requested
                    current_record->age = combining_pass_counter;

                    if (current_record->request.first == RequestType::PUSH) {
                        auto new_tail = new node(current_record->request.second);

                        if (tail)
                            tail->next = new_tail;
                        tail = new_tail;
                        if (!head)
                            head = tail;

                        current_record->request.first = RequestType::RESPONSE_PUSH;
                    } else if (current_record->request.first == RequestType::POP) {
                        if (head) {
                            current_record->request.second = head->data;
                            std::atomic_thread_fence(std::memory_order_release); // Make sure data is updated before
                                                                                 // signalling a response.
                            current_record->request.first = RequestType::RESPONSE_POP;

                            auto old_head = head;
                            head = head->next;
                            //delete old_head;
                        } else {
                            current_record->request.first = RequestType::RESPONSE_POP_FAIL;
                        }
                    }
                } else {
                    // Null requests are removed from the publication list if they become too old
                    if (combining_pass_counter - current_record->age > MAXIMUM_RECORD_AGE) {
                        if (!previous_record) {
                            publication_head.store(current_record->next);
                        } else {
                            previous_record->next = current_record->next;
                        }

                        current_record->active.store(false);
                    }
                }

                if (current_record->active) {
                    // Record the previous record in case we need to remove a record
                    previous_record = current_record;
                }
                current_record = current_record->next;
            }

            combiner_lock.clear();
        };

        /**
         * Adds request to thread's
         */
        publication_record* add_request(std::pair<RequestType, T> request) {
            static thread_local publication_record* thread_publication_record = nullptr;

            // First check if thread has a publication record
            if (thread_publication_record == nullptr) {
                // Allocate a publication record for thread
                thread_publication_record = new publication_record;
                thread_publication_record->next = nullptr;
                thread_publication_record->age = combining_pass_counter;
                thread_publication_record->active = false;
            }

            // Update node with new values
            thread_publication_record->request = std::move(request);

            // Make sure record is set to active
            if(!thread_publication_record->active) {
                thread_publication_record->active = true;

                // Append as the new head
                auto old_head = publication_head.load();
                do {
                    thread_publication_record->next = old_head;
                } while(!publication_head.compare_exchange_weak(old_head, thread_publication_record));
            }

            return thread_publication_record;
        }

    public:
        queue()
            : head(nullptr)
            , tail(nullptr)
            , combiner_lock(ATOMIC_FLAG_INIT)
            , combining_pass_counter(0) {
            // Setup first thread's publication record
            publication_head.store(nullptr);
        }

        ~queue() {
            // Pop queue nodes until head becomes nullptr (reached the tail).
            // Note: Both constructor and destructor can be assumed sequential, so thread-safety is not an issue.
            while (head != nullptr) {
                auto old_head = head;
                head = head->next;
                delete old_head;
            }
        }

        // Disallow copying a queue
        queue(const queue &other) = delete;
        queue &operator=(const queue &other) = delete;

        /**
         *  Returns true if the reference variable given is set to the value of the head of the queue (assuming the queue
         *  is not empty).
         */
        bool try_pop(T& return_value) {
            std::pair<RequestType, T> request = std::make_pair(RequestType::POP, T());
            auto record = add_request(request);

            // Node is prepared, spin waiting on a response or if the lock is open
            while (true) {
                if (record->request.first == RequestType::RESPONSE_POP) {
                    // Request processed; acknowledge and return
                    record->request.first = RequestType::NULL_RESPONSE;
                    return_value = std::move(record->request.second);
                    return true;
                } else if (record->request.first == RequestType::RESPONSE_POP_FAIL) {
                    record->request.first = RequestType::NULL_RESPONSE;
                    return false;
                } else if (!record->active) {
                    // Combiner decided that record is too old, update it and add request again to publication list
                    record = add_request(request);
                } else if (!combiner_lock.test_and_set()) {
                    // Got the lock
                    combiner();
                } else {
                    std::this_thread::yield();
                }
            }
        }

        /**
         * Pushes a new value onto the queue.
         */
        void push(T new_value) {
            std::pair<RequestType, T> request = std::make_pair(RequestType::PUSH, std::move(new_value));
            auto record = add_request(request);

            // Node is prepared, spin waiting on a response or if the lock is open
            while (true) {
                if (record->request.first == RequestType::RESPONSE_PUSH) {
                    // Request processed; acknowledge and return
                    record->request.first = RequestType::NULL_RESPONSE;
                    return;
                } else if (!record->active) {
                    // Combiner decided that record is too old, update it and add request again to publication list
                    record = add_request(std::move(request));
                } else if (!combiner_lock.test_and_set()) {
                    // Got the lock
                    combiner();
                } else {
                    // For programs where the thread count accessing this data structure is higher than the core count
                    // available, this prevents wasteful empty CAS loops waiting for a lock that is fighting to be
                    // scheduled by the OS.
                    std::this_thread::yield();
                }
            }
        }

        /**
         * Returns whether the queue is empty. It should be noted that another thread may have already added an
         * entry to the queue by the time the returned boolean is used.
         */
        bool empty() {
            return !head;
        }
    };
}

#endif //CCL_QUEUE_HPP


/*
 * Notes:
 *  - Singly linked list. The head is the top-most entry.
 *  - First make a thread-unsafe version to reference against.
 *
 */