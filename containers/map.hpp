//
// Created by Austin on 4/19/2015.
//  - Special thanks to http://kukuruku.co/hub/cpp/avl-trees for providing both the example source code and explanations
//    to help me understand this data structure.
//

#ifndef CCL_MAP_HPP
#define CCL_MAP_HPP

#include <cstddef>
#include <array>
#include <functional>
#include <utility>
#include <mutex>

namespace ccl {
    std::size_t const BUCKET_SIZE = 19; // May need to change this to scale with available cores on computer

    /**
     * A hashmap that supports concurrent operations.
     */
    template<typename KEY_TYPE, typename T>
    class map {
    private:
        std::hash<KEY_TYPE> hash_function;

        /**
         * A node entry in a binary tree.
         */
        struct node {
            T value;
            std::size_t hash_value;
            std::uint8_t height; // 8 bit is enough because height = log2(num_entries/bucket_size), which for 8 bits
                                 // can hold roughly 10^78 entries...

            node* lesser_key_node; // left
            node* greater_key_node; // right

            node(T value_, std::size_t hash)
                    : value(value_)
                    , hash_value(hash)
                    , height(1)
                    , lesser_key_node(nullptr)
                    , greater_key_node(nullptr) {
            }
        };

        // 19 buckets are used to hold map entries bucket = (key % bucket_size)
        std::array<node*, BUCKET_SIZE> buckets;
        // Each bucket gets its own mutex so that lock contention is dramatically reduced
        std::array<std::mutex, BUCKET_SIZE> bucket_mutexes;

        /**
         * Returns height of a node (empty node is zero).
         */
        inline std::uint8_t height(node* node_) {
            return node_ ? node_->height : 0;
        }

        /**
         * Returns the balance factor of a given node.
         */
        inline unsigned int balance_factor(node* node_) {
            return height(node_->greater_key_node) - height(node_->lesser_key_node);
        }

        /**
         * Corrects the height value for the given node (assuming height is correct for children nodes).
         */
        void fix_height(node* node_) {
            auto lesser_node_height = height(node_->lesser_key_node);
            auto greater_node_height = height(node_->greater_key_node);
            node_->height = (lesser_node_height > greater_node_height ? lesser_node_height : greater_node_height) + 1;
        }

        /**
         * Right rotation around node_.
         */
        inline node* rotate_right(node* node_) {
            node* result = node_->lesser_key_node;
            node_->lesser_key_node = result->greater_key_node;
            result->greater_key_node = node_;
            fix_height(node_);
            fix_height(result);
            return result;
        }

        /**
         * Left rotation around node_.
         */
        inline node* rotate_left(node* node_) {
            node* result = node_->greater_key_node;
            node_->greater_key_node = result->lesser_key_node;
            result->lesser_key_node = node_;
            fix_height(node_);
            fix_height(result);
            return result;
        }

        /**
         * Balances provided node.
         */
        node* balance(node* node_) {
            fix_height(node_);
            if (balance_factor(node_) == 2) {
                // Too high on right side
                if (balance_factor(node_->greater_key_node) < 0)
                    node_->greater_key_node = rotate_right(node_->greater_key_node);
                return rotate_left(node_);
            }
            if (balance_factor(node_) == -2) {
                // Too high on left side
                if (balance_factor(node_->lesser_key_node) > 0)
                    node_->lesser_key_node = rotate_left(node_->lesser_key_node);
                return rotate_right(node_);
            }

            // Already balanced
            return node_;
        }

        /**
         * Inserts node for tree with provided base node.
         */
        node* insert(node* base_node, T value, std::size_t hash_value) {
            if (!base_node)
                return new node(std::move(value), std::move(hash_value));
            if (hash_value < base_node->hash_value)
                base_node->lesser_key_node = insert(base_node->lesser_key_node, std::move(value), std::move(hash_value));
            else if (hash_value > base_node->hash_value)
                base_node->greater_key_node = insert(base_node->greater_key_node, std::move(value), std::move(hash_value));
            else if (hash_value == base_node->hash_value)
                base_node->value = std::move(value);

            return balance(base_node);
        }

        /**
         * Finds node with smallest hash in tree.
         */
        node* find_minimum_hash(node* base_node) {
            return base_node->lesser_key_node ? find_minimum_hash(base_node->lesser_key_node) : base_node;
        }

        /**
         * Removes the node with the smallest hash in tree.
         */
        node* remove_minimum_hash(node* base_node) {
            if (base_node->lesser_key_node == 0)
                return base_node->greater_key_node;

            base_node->lesser_key_node = remove_minimum_hash(base_node->lesser_key_node);

            return balance(base_node);
        }

        /**
         * Remove node with provided key from tree.
         */
        node* remove(node* base_node, std::size_t hash_value, bool& result) {
            if (!base_node) return nullptr;
            if (hash_value < base_node->hash_value)
                base_node->lesser_key_node = remove(base_node->lesser_key_node, hash_value, result);
            else if (hash_value > base_node->hash_value)
                base_node->greater_key_node = remove(base_node->greater_key_node, hash_value, result);
            else {
                auto left_node = base_node->lesser_key_node;
                auto right_node = base_node->greater_key_node;
                delete base_node;
                result = true; // Removed element

                if (!right_node)
                    return left_node;

                auto min = find_minimum_hash(right_node);
                min->greater_key_node = remove_minimum_hash(right_node);
                min->lesser_key_node = left_node;

                return balance(min);
            }
        }

        /**
         * Recursively find all children and delete them.
         */
        node* delete_children(node* base_node) {
            if (base_node) {
                delete delete_children(base_node->lesser_key_node);
                delete delete_children(base_node->greater_key_node);
            }

            return base_node;
        }

    public:
        map() {
            // Initialize all buckets to empty
            for (std::size_t index = 0; index < BUCKET_SIZE; ++index) {
                buckets[index] = nullptr;
            }
        }

        ~map() {
            // Clean up all nodes in the buckets
            for (std::size_t index = 0; index < BUCKET_SIZE; ++index) {
                delete delete_children(buckets[index]);
            }
        }

        /**
         * Inserts value into map.
         */
        void insert(KEY_TYPE key, T value) {
            auto hash = hash_function(key);
            auto bucket_to_add = hash % BUCKET_SIZE;
            std::lock_guard<std::mutex> lock(bucket_mutexes[bucket_to_add]);

            // Navigate through bucket to find an open node
            auto current_node = buckets[bucket_to_add];
            buckets[bucket_to_add] = insert(current_node, std::move(value), std::move(hash));
        }

        /**
         * Modifies reference to value at corresponding key, returning true if it exists.
         */
        bool try_at(KEY_TYPE key, T& value) {
            auto hash = hash_function(key);
            auto bucket_to_add = hash % BUCKET_SIZE;
            std::lock_guard<std::mutex> lock(bucket_mutexes[bucket_to_add]);

            // Navigate through bucket to find an open node
            auto current_node = buckets[bucket_to_add];
            while (current_node) {
                if (hash > current_node->hash_value) {
                    current_node = current_node->greater_key_node;
                } else if (hash < current_node->hash_value) {
                    current_node = current_node->lesser_key_node;
                } else {
                    value = current_node->value;
                    return true;
                }
            }

            return false;
        }

        /**
         * If map has entry with provided key, deletes entry and returns true.
         */
        bool try_erase(KEY_TYPE key) {
            auto hash = hash_function(key);
            auto bucket_to_add = hash % BUCKET_SIZE;
            std::lock_guard<std::mutex> lock(bucket_mutexes[bucket_to_add]);

            bool result = false;
            buckets[bucket_to_add] = remove(buckets[bucket_to_add], std::move(hash), result);

            if (result)
                return true;

            return false;
        }
    };
}

#endif //CCL_MAP_HPP
