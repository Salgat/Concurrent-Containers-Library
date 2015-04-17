//
// Created by Austin Salgat on 4/1/2015.
//  - Special thanks to A. Williams and his book "C++ Concurrency in Action" which was the bases for this library.
//  - Note: The code, especially the concurrent implementations, are heavily commented since concurrent programming
//    can be very hard to keep track of (at least for me!).
//

#ifndef CCL_CCL_HPP
#define CCL_CCL_HPP

namespace ccl {
    unsigned int const MAXIMUM_RECORD_AGE = 100; // After 100 operations, old records are removed from publication list
}


// Concurrent stack (LIFO)
#include "containers/stack.hpp"
// Doubly-linked list (for fast insertion/erase but slow iteration)
//#include "containers/list.hpp"
// Concurrent Queue (FIFO)
#include "containers/queue.hpp"
// Array implemented vector (for fast read but slow write)
//#include "containers/vector.hpp"
// Operates like a stack/queue in that data can be pushed into it, but pop randomly removes one pushed entry (there
// are no guarantees about order).
#include "containers/data_pool.hpp"

#endif //CCL_CCL_HPP
