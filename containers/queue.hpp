//=====================================================================================================================================
//The MIT License (MIT)
//Copyright (c) 2015 Austin Salgat
//
//Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files 
//(the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, 
//merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished 
//to do so, subject to the following conditions:
//The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES 
//OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE 
//LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR 
//IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//=====================================================================================================================================

#ifndef CCL_QUEUE_HPP
#define CCL_QUEUE_HPP

namespace ccl {
    /**
     * Concurrent queue. A simplified version of std::queue that allows for concurrent access. As long as std::atomic is
     * implemented as lock-free, this class is lock-free (but not wait-free).
     */
    template<typename T>
    class queue {
    private:
        struct node {
            node* next;
            T data;
            std::atomic<int> counter; // When this hits zero, it can be deleted

            node(T data_)
                    : data(data_)
                    , next(nullptr)
                    , counter(0) { }
        };

        std::atomic<node*> head;
        std::atomic<node*> tail;
        std::atomic<std::size_t> size;

        ccl::stack<node*> nodes_to_delete;

        void clean_nodes() {
            node* old_node;
            while(nodes_to_delete.try_pop(old_node)) {
                if (old_node != nullptr and old_node->counter.load() < 1)
                    old_node->counter--;
                    delete old_node;
            }
        }

    public:
        queue()
            : head(nullptr)
            , tail(nullptr)
            , size(0){

        }

        ~queue() {
            // Loop through linked list until all entries are deleted.
            while(size.load() > 0) {
                auto old_head = head.load();
                if (old_head != nullptr) {
                    node* new_head = old_head->next;
                    head.store(new_head);
                    delete old_head;
                }
                --size;
            }
        }

        /**
         * Appends a new node to the tail.
         */
        void push(T new_value) {
            auto new_tail = new node(new_value);
            auto old_tail = tail.load();
            do {
                if (old_tail != nullptr)
                    // This could be an unsafe action, since while CAS is happening, old_tail->next could be overwritten
                    old_tail->next = new_tail;
            } while(!tail.compare_exchange_weak(old_tail, new_tail) and old_tail->next == new_tail);

            // If head is nullptr (empty), head == tail
            // Note: Make sure tail doesn't change, otherwise head will be set to the wrong tail.
            // Note2: The comparison of empty_node and head.load() is to make sure that we didn't just treat a popped
            // node as the head (between the head.cas and tail.cas, head could have been popped).
            node* empty_node;
            node* current_tail = tail.load();
            do {
                empty_node = nullptr;
                head.compare_exchange_strong(empty_node, current_tail);
            } while (!tail.compare_exchange_strong(current_tail, current_tail) or empty_node != head.load());

            ++size;
        }

        /**
         * If possible, removes the oldest entry from the front of the queue, replacing it with the second oldest entry
         * still in the queue.
         */
        bool try_pop(T& return_value) {
            clean_nodes();

            if (size.load() == 0)
                return false;

            auto old_head = head.load();
            node* new_head = nullptr;
            do {
                if (old_head != nullptr)
                    new_head = old_head->next;
            } while(!head.compare_exchange_weak(old_head, new_head));

            if (old_head != nullptr) {
                old_head->counter++;
                if (old_head->counter.load() > 0) {
                    old_head->counter--;
                    return_value = old_head->data;

                    nodes_to_delete.push(old_head);
                    --size;

                    return true;
                }
            }

            return false;
        }

        bool empty() {
            return head.load() == nullptr;
        }
    };
}

#endif //CCL_QUEUE_HPP


// Todo:
//   - Go over the use of the node counter, it doesn't seem right.