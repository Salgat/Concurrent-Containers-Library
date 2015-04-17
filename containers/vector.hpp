//
// Created by Austin on 4/2/2015.
//

#ifndef CCL_VECTOR_HPP
#define CCL_VECTOR_HPP

#include "stack.hpp"

#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <utility>
#include <tuple>
#include <algorithm>

template<typename T>
class vector_iterator;

namespace ccl {
    /**
     * A vector that supports thread-safe writes (using locking) and lock-free and wait-free concurrent reading (for
     * platforms that support lock-free atomics, including x86/x64).
     *
     * In general, managed arrays (including std::vector) are very bad at concurrency since they usually become invalid
     * during writes, which means all reads and writes require locks. This vector uses a double buffer of shared
     * pointers to allow for higher performance reading (at the cost of slow writing), since it will only block
     * write actions so that they are done sequentially.
     *
     * Note: For now, the thread can only grow in capacity (size can shrink however).
     */
    std::size_t const INITIAL_VECTOR_SIZE = 7;
    float const VECTOR_GROWTH_FACTOR = 1.5; // See https://github.com/facebook/folly/blob/master/folly/docs/FBVector.md

    template<typename T, typename allocator_type = std::allocator<T*>>
    class vector {
    // Todo: Instead of an array of raw references (T**), try using an array of shared_ptrs (shared_ptr<T>*)
    private:
        typedef T* data_pointer;
        allocator_type allocator;

        /*
         * Holds array and basic information about it.
         */
        struct array_container {
            data_pointer* array;
            std::size_t size;
            std::size_t capacity;
        };

        // For thread-safety when reading values, erased/cleared values should be deleted after reading is complete.
        // Add them to this stack so that they can be deleted later safely.
        // Todo: Just use a normal std::stack, which is more efficient since we're just going to be reading and writing
        // to the stack inside writes (which are locked to be sequential).
        ccl::stack<data_pointer> entries_to_delete;

        // Two arrays exist as a double buffer (to allow one to be read while the other is written to).
        // Determines which array is used for which operations (read or write)
        // Note: read_vector accesses are always safe for two reasons:
        //  - Aligned pointer reads and writes are atomic.
        //  - The read vector always points to a valid vector.
        std::atomic<array_container*> read_container;
        std::atomic<array_container*> write_container;

        std::mutex write_mutex;
        // Used as a rudimentary semaphore
        // Note: If try_at() were to throw an exception, this would stay incremented. Only thing I can think of that
        // would throw is a failed de-reference. Look into this, might need to create a class similar to lock_guard that
        // would automatically decrement 'reading' upon exiting the function and destructing.
        std::atomic<unsigned int> reading;

        /**
         * Expands capacity of array to match at least the size specified.
         */
        inline void resize_array(array_container* container, std::size_t new_minimum_size, bool copy_contents) {
            if (container->capacity >= new_minimum_size) {
                // Container is already large enough for new size.
                container->size = new_minimum_size;
                return;
            }

            // Need to create a new array of a larger size and copy over contents of old.
            std::size_t new_array_size = new_minimum_size * VECTOR_GROWTH_FACTOR;
            auto new_array = allocator.allocate(new_array_size);

            // Only copy contents if specified, since some functions will write over the array later anyways.
            if (copy_contents) {
                // Copy over contents (pointers) to new array.
                for (std::size_t index = 0; index < container->size; ++index) {
                    new_array[index] = container->array[index];
                }
            }

            // Finally update container to new array and deallocate old one.
            allocator.deallocate(container->array, container->capacity);

            container->array = new_array;
            container->size = new_minimum_size;
            container->capacity = new_array_size;
        }

        /**
         * Resize array specialized to include an insert (for performance).
         */
        inline void resize_array_for_insert(array_container* container, std::size_t position, T insert_value) {
            data_pointer* target_array;
            auto new_array_size = container->size+1;
            auto old_array_capacity = container->capacity;
            bool new_array_allocated = false;

            // First increase size of array by 1.
            if (container->capacity >= new_array_size) {
                // Container is already large enough for new size.
                target_array = container->array;
            } else {
                // Need to create a new array of a larger size and copy over contents of old.
                std::size_t new_array_capacity = new_array_size * VECTOR_GROWTH_FACTOR;
                target_array = allocator.allocate(new_array_capacity);
                new_array_allocated = true;
                container->capacity = new_array_capacity;

                // Copy over contents up to insert position (this is already the case if capacity is large enough).
                for (std::size_t index = 0; index < position; ++index) {
                    target_array[index] = container->array[index];
                }
            }
            container->size = new_array_size;

            // Copy contents shifted 1 position further.
            for (std::size_t index = position+1; index < new_array_size; ++index) {
                target_array[index] = container->array[index-1];
            }

            // Finally insert new value.
            target_array[position] = new T(std::move(insert_value));

            // Deallocate old array and set write_array to new array.
            if (new_array_allocated) {
                allocator.deallocate(container->array, old_array_capacity);
                container->array = target_array;
            }
        }

        /**
         * Safely update read_array to the newly modified write_array.
         */
        inline void update_read_array() {
            auto read_array_container = read_container.load();
            auto write_array_container = write_container.load();

            // Now need to copy contents of newly valid container over to the old outdated container.
            auto invalid_container = read_array_container;
            auto valid_container = write_array_container;
            resize_array(invalid_container, valid_container->size, false);
            for (std::size_t index = 0; index < valid_container->size; ++index) {
                invalid_container->array[index] = valid_container->array[index];
            }

            // Store the newly valid container in read_container.
            read_container.store(write_array_container);
            write_container.store(read_array_container);

            // To prevent the ABA problem, block until any reads are finished. The reason is, during the CAS loop for
            // try_at(), preventing the read_container from being changed to the write_container and then changed again
            // back to the read_container, which to the CAS loop goes undetected, as if the read_container was never
            // touched.
            while(reading.load() > 0);
            // clean_old_entries() is wrapped  by two read-semaphore checks to guarantee that a read (try_at()) won't
            // be de-referencing a deleted pointer. This is because, between two read-semaphore checks, no other writes
            // occur, which means that no thread reading the vector right now can have access to an invalid pointer
            // outside the CAS loop. With only one read-semaphore check, another write could occur outside the CAS loop
            // invalidating the pointer (since both read and write arrays share the same pointers).
            clean_old_entries();
            while(reading.load() > 0);
        }

        /**
         * Deletes objects that are waiting to be erased.
         */
        inline void clean_old_entries() {
            data_pointer entry_to_delete;
            while(entries_to_delete.try_pop(entry_to_delete)) {
                delete entry_to_delete;
            }
        }

        /**
         * Erases entry in write vector then swaps with read array (helper method).
         */
        inline void erase(std::size_t position, array_container* write_container_pointer, std::size_t array_size) {
            // Add pointer at position to stack to be erased then shift over all contents.
            entries_to_delete.push(write_container_pointer->array[position]);
            for (std::size_t index = position; index < array_size-1; ++index) {
                write_container_pointer->array[index] = write_container_pointer->array[index+1];
            }
            write_container_pointer->size = array_size-1;
            update_read_array();
        }

    public:
        typedef vector_iterator<T> iterator;

        vector() {
            // Setup read_container and write_container
            auto read_array = allocator.allocate(INITIAL_VECTOR_SIZE);
            auto read_array_container = new array_container;
            read_array_container->array = read_array;
            read_array_container->size = 0;
            read_array_container->capacity = INITIAL_VECTOR_SIZE;
            read_container.store(read_array_container);

            auto write_array = allocator.allocate(INITIAL_VECTOR_SIZE);
            auto write_array_container = new array_container;
            write_array_container->array = write_array;
            write_array_container->size = 0;
            write_array_container->capacity = INITIAL_VECTOR_SIZE;
            write_container.store(write_array_container);

            reading.store(0);
        }

        ~vector() {
            auto read_array_container = read_container.load();
            auto write_array_container = write_container.load();

            // Iterate though all entries and delete them before deallocating the arrays
            for (std::size_t index = 0; index < read_array_container->size; ++index) {
                //delete read_array_container->array[index];
            }
            for (std::size_t index = 0; index < write_array_container->size; ++index) {
                //delete write_array_container->array[index];
            }

            allocator.deallocate(read_array_container->array, read_array_container->capacity);
            allocator.deallocate(write_array_container->array, write_array_container->capacity);

            delete read_array_container;
            delete write_array_container;
        }

        /**
         * Updates provided reference with value at specified index and returns true if element exists.
         */
        bool try_at(std::size_t index, T& value) {
            reading++;
            bool successful_read;
            data_pointer value_pointer;

            // CAS loop to ensure that the array being read from hasn't changed.
            // Note: Even if the array is being modified as we read from it, it is thread safe because of many reasons:
            //  1) We are only reading pointers, which are atomically read (for x86/x64).
            //  2) The arrays only grow in size, so we always have some pointer to read, even if its object was erased.
            //  3) Any pointers that are to be erased won't be erased until after a read is complete, due to true
            //     deconstructing happening at the end with clean_old_entries().
            // Finally, every time the read container pointer is modified, the operation will block all further writes
            // until a read operation finishes, to prevent the ABA problem.
            auto reading_container = read_container.load();
            do {
                if (index < reading_container->size) {
                    successful_read = true;
                    value_pointer = reading_container->array[index];
                } else {
                    successful_read = false;
                }
            } while (!read_container.compare_exchange_weak(reading_container, read_container));

            if (successful_read) {
                value = *value_pointer;
            }

            reading--;

            return successful_read;
        }

        /**
         * Push back value to end of vector.
         */
        void push_back(T value) {
            std::lock_guard<std::mutex> lock(write_mutex);
            auto write_container_pointer = write_container.load();

            auto new_array_size = write_container_pointer->size+1;
            resize_array(write_container_pointer, new_array_size, true);
            write_container_pointer->array[new_array_size-1] = new T(std::move(value));

            update_read_array();
        }

        /**
         * Tries to remove last entry of vector, returning true if succeeded.
         */
        bool try_pop_back() {
            std::lock_guard<std::mutex> lock(write_mutex);
            auto write_container_pointer = write_container.load();

            auto array_size = write_container_pointer->size;
            if (array_size > 0) {
                entries_to_delete.push(write_container_pointer->array[array_size-1]);
                write_container_pointer->size = array_size-1;
                update_read_array();
                return true;
            }

            return false;
        }

        /**
         * Inserts variable at position provided.
         */
        bool try_insert(std::size_t position, T value) {
            std::lock_guard<std::mutex> lock(write_mutex);
            auto write_container_pointer = write_container.load();

            if (position >= write_container_pointer->size)
                return false;

            // Resize array and insert new value.
            resize_array_for_insert(write_container_pointer, position, std::move(value));

            update_read_array();

            return true;
        }

        /**
         * Returns the size of the current vector. May change value due to concurrent writes.
         */
        std::size_t size() {
            // Size can change which means that as long as we can safely read something (even if it is invalid), we can
            // return it. Reading/Writing an aligned 32/64 bit memory is a thread-safe operation in x86/x86_64, so we
            // can simply read whatever happens to be read for size.
            return read_container.load()->size;
        }

        /**
         * Clears both vectors (while preserving their capacity).
         *
         * Note: If the program is still holding onto a shared_ptr, it will not be deconstructed until that program
         * releases/deconstructs that shared_ptr.
         */
        void clear() {
            std::lock_guard<std::mutex> lock(write_mutex);
            auto write_container_pointer = write_container.load();

            // Add all existing entries in the array to the "entries_to_delete" stack for erasing later.
            auto old_array_size = write_container_pointer->size;
            for (std::size_t index = 0; index < old_array_size; ++index) {
                entries_to_delete.push(write_container_pointer->array[index]);
            }
            write_container_pointer->size = 0;

            update_read_array();

            return;
        }

        /**
         * Erases entry at position provided, returning true if successful. This should be used carefully as other
         * threads may be changing the size and contents of the vector, changing what is at the position provided. This
         * is most useful for removing the front of the a vector (aka pop_front()) or using a lock to ensure the
         * value to be erased is at the position specified.
         */
        bool try_erase(std::size_t position) {
            std::lock_guard<std::mutex> lock(write_mutex);
            auto write_container_pointer = write_container.load();

            auto array_size = write_container_pointer->size;
            if (position >= array_size)
                return false;

            erase(position, write_container_pointer, array_size);

            return true;
        }

        /**
         * Tests if the value provided is equal to the the value at the position given, and if so, returns true and
         * erases the entry. This function exists because try_erase() does not guarantee that the element at the
         * position provided has not changed due to another thread.
         */
        bool test_and_erase(std::size_t position, T value) {
            std::lock_guard<std::mutex> lock(write_mutex);
            auto write_container_pointer = write_container.load();

            auto array_size = write_container_pointer->size;
            if (position >= array_size)
                return false;

            // Since a value exists at the position given, test to see if it is equal to the value provided.
            if (value != write_container_pointer->array[position])
                return false;

            erase(position, write_container_pointer, array_size);

            return true;
        }

        /**
         * Prints the contents of the read_array. Should only use for DEBUG purposes.
         */
        void debug_print_contents() {
            return;
        }

        iterator begin() {
            return iterator(0, this);
        }

        iterator end() {
            return iterator(size(), this);
        }
    };
}

/**
 * A specialized iterator class for ccl::vector. Instead of holding a raw pointer to a value in the vector, it holds a
 * reference to the vector and the position the iterator is at. A de-reference returns a shared_ptr<T> since we can't
 * guarantee that an entry at the given position still exists (due to concurrent writes from other threads), which
 * returns a nullptr reference if no value exists.
 */
template<typename T>
class vector_iterator {
private:
    std::size_t position;
    ccl::vector<T>* vector_for_iterator;

public:
    vector_iterator(std::size_t starting_position, ccl::vector<T>* vector_reference)
        : position(starting_position)
        , vector_for_iterator(vector_reference) {
    }

    vector_iterator(const vector_iterator<T>& other)
        : position(other.position)
        , vector_for_iterator(other.vector_for_iterator) {
    }

    vector_iterator(vector_iterator&& other)
        : position(other.position)
        , vector_for_iterator(other.vector_for_iterator) {
    }

    vector_iterator& operator=(const vector_iterator& other) {
        if (&other != this) {
            position = other.position;
            vector_for_iterator = other.vector_for_iterator;
        }

        return *this;
    }

    vector_iterator& operator=(vector_iterator&& other) {
        if (&other != this) {
            position = other.position;
            vector_for_iterator = other.vector_for_iterator;
        }

        return *this;
    }

    ~vector_iterator() {}


    std::shared_ptr<T> operator*() {
        T value;
        if (vector_for_iterator->try_at(position, value))
            return std::make_shared<T>(std::move(value));

        return nullptr;
    }

    vector_iterator<T>& operator++() {
        position++;
        return *this;
    }

    vector_iterator<T>& operator--() {
        position--;
        return *this;
    }

    vector_iterator<T>& operator+(std::size_t offset) {
        position += offset;
        return *this;
    }

    vector_iterator<T>& operator+=(std::size_t offset) {
        position += offset;
        return *this;
    }

    vector_iterator<T>& operator-(std::size_t offset) {
        position -= offset;
        return *this;
    }

    vector_iterator<T>& operator-=(std::size_t offset) {
        position -= offset;
        return *this;
    }

    bool operator==(const vector_iterator& other) {
        if (vector_for_iterator == other.vector_for_iterator and position == other.position)
            return true;
        return false;
    }

    bool operator!=(const vector_iterator& other) {
        if (*this == other)
            return false;
        return true;
    }
};


#endif //CCL_VECTOR_HPP

// Todo:
//  - Implement move semantics.
//  - Optimize resizing array to reduce copying over arrays multiple times.
//  - Implement erase.
//  - Work on pushing benchmarks harder and finding a better growth constant. Unlike sequential std::vector,
//    growths (which take a long time) need to be balanced with both array copies (which take moderate time and take
//    longer with larger arrays) and the overhead of each write, which is, itself, quite large. Larger growth rate,
//    less resize calls which means less time spent in the write lock.
//      - 1.5 might still be the best, check out results.txt
//  - Make sure this runs faster than just straight up wrapping a lock on a vector. After all, what's the point if it
//    can't even beat that?
//  - Clean up code, support rule of 5, etc. This is still very much slapped together.
//  - Each value is individually allocated, which creates poor locality of reference.