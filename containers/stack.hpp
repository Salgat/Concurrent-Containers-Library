//
// Created by Austin Salgat on 4/1/2015.
//

#ifndef CCL_STACK_HPP
#define CCL_STACK_HPP

#include <utility>
#include <memory>
#include <atomic>

namespace ccl {
    /**
     * Sequential (thread unsafe) version of the stack, used as a reference.
     */
    template<typename T>
    class sequential_stack {
    private:
        struct node {
            std::shared_ptr<node> next;
            T data;

            node(T data_)
                    : data(std::move(data_)) { }
        };

        std::shared_ptr<node> head;

    public:
        sequential_stack() {}

        // Disallow copying stacks
        sequential_stack(const sequential_stack &other) = delete;
        sequential_stack &operator=(const sequential_stack &other) = delete;

        bool try_pop(T& return_value) {
            if (!head) {
                return false;
            } else {
                return_value = std::move(head->data);
                head = head->next;
                return true;
            }
        }

        void push(T new_value) {
            // Create a new node and make it the new head
            auto new_node = std::make_shared<node>(std::move(new_value));
            new_node->next = head;
            head = new_node;
        }

        bool empty() {
            return !head;
        }
    };

    /**
     * Concurrent stack. A simplified version of std::stack that allows for concurrent access. As long as std::atomic is
     * implemented as lock-free, this class is lock-free (but not wait-free).
     */
    template<typename T>
    class stack {
    private:
        struct node {
            node* next;
            T data;

            node(T data_)
                    : data(std::move(data_))
                    , next(nullptr) { }
        };

        std::atomic<node*> head;

    public:
        stack()
            : head(nullptr){}

        ~stack() {
            // Pop stack nodes until head becomes nullptr (reached the tail).
            // Note: Both constructor and destructor can be assumed sequential, so thread-safety is not an issue.
            while (head.load() != nullptr) {
                auto old_head = head.load();
                head.store(head.load()->next);
                delete old_head;
            }
        }

        // Disallow copying a stack
        stack(const stack &other) = delete;
        stack &operator=(const stack &other) = delete;

        /**
         *  Returns true if the reference variable given is set to the value of the head of the stack (assuming the stack
         *  is not empty).
         */
        bool try_pop(T& return_value) {
            // Store the current head, and have head point to the next entry in the stack (in an atomic fashion).
            // Make sure, if stack is emptied at any time during this loop, that we end the loop before accessing
            // old_head->next on a nullptr.
            auto old_head = head.load();
            while (old_head != nullptr and !head.compare_exchange_weak(old_head, old_head->next));

            // Now old_head holds the old head that is popped (nullptr if old_head->next didn't exist and stack was empty).
            if (old_head != nullptr) {
                return_value = std::move(old_head->data);
                delete old_head;
                return true;
            }
            return false;
        }

        /**
         * Pushes a new value onto the stack.
         */
        void push(T new_value) {
            // Create a new node and make it the new head
            auto new_node = new node(std::move(new_value));

            // new_node now becomes the next head. Need to make sure new_node->next points to the current head
            // before making it the new head, otherwise the sequence of nodes is out-of-order (another node
            // might have already changed the head, which would be overwritten again deleting that node).
            new_node->next = head.load();
            while(!head.compare_exchange_weak(new_node->next, new_node));
        }

        /**
         * Returns whether the stack is empty. It should be noted that another thread may have already added an
         * entry to the stack by the time the returned boolean is used.
         */
        bool empty() {
            return !head.load();
        }
    };
}

#endif //CCL_STACK_HPP


/*
 * Notes:
 *  - Singly linked list. The head is the top-most entry.
 *  - First make a thread-unsafe version to reference against.
 *
 */