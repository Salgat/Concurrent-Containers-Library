//
// Created by Austin on 4/17/2015.
//

#ifndef CCL_DATA_POOL_HPP
#define CCL_DATA_POOL_HPP

#include <cstddef>
#include <vector>

namespace ccl {
    std::size_t const INITIAL_SIZE = 11;
    double const GROWTH_RATE = 1.5; // Growth rate (size increase) of each successive data pool entry.

    /**
     * Allows data to be pushed into a "pool" of data, where pops remove one entry with no guarantee about which is
     * removed (random order for popping).
     */
    template<typename T>
    class data_pool {
    private:
        struct node {
            std::atomic_flag available_write;
            std::atomic_flag available_read;
            T data;

            node ()
                : available_write(ATOMIC_FLAG_INIT)
                , available_read(ATOMIC_FLAG_INIT) {
                // Lock available_read until it is available for reading
                available_read.test_and_set();
            }
        };

        /**
         * Holds an array of nodes.
         */
        struct pool {
            std::vector<node> node_array;
            std::size_t size;
            pool* next;

            pool(std::size_t size_)
                : node_array(size_)
                , next(nullptr)
                , size(size_) {
            }
        };

        std::atomic<pool*> pool_head;

    public:
        data_pool() {
            // Initialize first data pool
            pool_head = new pool(INITIAL_SIZE);
        }

        ~data_pool() {
            auto pool_entry = pool_head.load();
            while (pool_entry) {
                auto old_entry = pool_entry;
                pool_entry = pool_entry->next;
                delete old_entry;
            }
        }

        /**
         * Go through the pool to find an open entry to add to.
         */
        void push(T value) {
            while (true) {
                auto current_pool = pool_head.load();
                unsigned int count = 0;
                while (current_pool) {
                    for (auto &node_entry : current_pool->node_array) {
                        if (!node_entry.available_write.test_and_set()) {
                            // Node is available to use
                            node_entry.data = std::move(value);

                            // Unlocks the node for reading
                            node_entry.available_read.clear();

                            return;
                        }
                    }

                    current_pool = current_pool->next;
                    ++count;
                }

                // If we reach this point, we need to expand the pool to try again
                auto old_head = pool_head.load();
                auto new_pool = new pool(old_head->size * GROWTH_RATE);
                do {
                    new_pool->next = old_head;
                } while (!pool_head.compare_exchange_weak(old_head, new_pool));
            }
        }

        /**
         * Searches the pool for an available data to pop, returning true if it is able.
         */
        bool try_pop(T& return_value) {
            auto current_pool = pool_head.load();
            while (current_pool) {
                // Iterate through each node looking for a node
                for (auto &node_entry : current_pool->node_array) {
                    if (!node_entry.available_read.test_and_set()) {
                        // Gained the lock on this entry, copy the value and make it available to be overwritten
                        return_value = node_entry.data;
                        node_entry.available_write.clear();
                        return true;
                    }
                }

                current_pool = current_pool->next;
            }

            // No pools left to read
            return false;
        }

        /**
         * Removes all entries and "resets" the data pool.
         */
        void clear() {
            // Sets the pool_head to a completely new data pool
            auto old_head = pool_head.load();
            auto new_head = new pool(INITIAL_SIZE);
            // The while loop is necessary since we need to know what the previous head was before we set the new one
            while(!pool_head.compare_exchange_weak(old_head, new_head));


            // Todo: Add a counter for each pool that is incremented whenever someone is using it
            // would be much easier if shared_ptr was atomic!
            // Clean up the old pools
            while (old_head) {
                auto old_entry = old_head;
                old_head = old_head->next;
                //delete old_entry;
            }
        }
    };
}

#endif //CCL_DATA_POOL_HPP

/**
 *  Notes:
 *    - Consider using shared_ptr for pools to handle destructing.
 */