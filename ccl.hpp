//
// Created by Austin Salgat on 4/1/2015.
//  - Special thanks to A. Williams and his book "C++ Concurrency in Action" which was the bases for this library.
//  - Note: The code, especially the concurrent implementations, are heavily commented since concurrent programming
//    can be very hard to keep track of (at least for me!).
//

#ifndef CCL_CCL_HPP
#define CCL_CCL_HPP

// Concurrent stack (LIFO)
#include "containers/stack.hpp"
// Doubly-linked list (for fast insertion/erase but slow iteration)
//#include "containers/list.hpp"
// Concurrent Queue (FIFO)
#include "containers/queue.hpp"
// Array implemented vector (for fast read but slow write)
#include "containers/vector.hpp"

#endif //CCL_CCL_HPP
