Concurrent Containers Library
=================

The Concurrent Containers Library (CCL) is a personal attempt to create concurrent containers using C++14 and being header only. The interfaces for each container are different than the comparable standard library version. It should be noted that this is a naive implementation of concurrent containers, and should not be used for a serious project (there are no guarantees that it is thread-safe, despite having been tested).

Concurrent Stack
-----------------

Concurrent Stack is a LIFO stack implemented using flat-combining that supports the following methods,
* bool try_pop(T& value)
* void push(T value)
* bool empty()

Below is an example of using ccl::stack to push and pop a string.

```c++
#include "ccl.hpp"
#include <iostream>

int main() {
    std::cout << "Starting concurrent stack test" << std::endl;
    ccl::stack<std::string> stack;
	
    stack.push("Hello!");
	
    std::string popped_value = "Empty";
    if(stack.try_pop(popped_value)) {
        std::cout << "Popped value: " << popped_value << std::endl;
    } else {
        std::cout << "Value did not pop, variable is still: " << popped_value << std::endl;
    }
}
```

Concurrent Queue
-----------------

Concurrent Queue is a FIFO singly-linked list implemented using flat-combining that supports the following methods,
* void push(T value)
* bool try_pop(T& value)
* bool empty()

Below is an example of using ccl::queue to push and pop a string.

```c++
#include "ccl.hpp"
#include <iostream>

int main() {
    std::cout << "Starting concurrent queue test" << std::endl;
    ccl::queue<std::string> queue;
	
    queue.push("Hello!");
	
    std::string popped_value = "Empty";
    if(queue.try_pop(popped_value)) {
        std::cout << "Popped value: " << popped_value << std::endl;
    } else {
        std::cout << "Value did not pop, variable is still: " << popped_value << std::endl;
    }
}
```

Concurrent "Data Pool"
-----------------

Concurrent Data Pool is a lock-free alternative to concurrent queue and stack in that it does not guarantee the order in which data is popped. The following methods are supported,
* void push(T value)
* bool try_pop(T& value)
* void clear()

Below is an example of using ccl::data_pool to push and pop a string.

```c++
#include "ccl.hpp"
#include <iostream>

int main() {
    std::cout << "Starting concurrent data pool test" << std::endl;
    ccl::data_pool<std::string> pool;
	
    pool.push("Hello!");
	
    std::string popped_value = "Empty";
    if(pool.try_pop(popped_value)) {
        std::cout << "Popped value: " << popped_value << std::endl;
    } else {
        std::cout << "Value did not pop, variable is still: " << popped_value << std::endl;
    }
}
```

Concurrent Data Pool is an attempt to build a data structure that is more concurrent friendly than the stack and queue. The data pool uses a successive list of arrays with simple atomic_flags to show whether it can be written to or read from. The idea is that by giving up control over the order of the data, we can make it more concurrent friendly. Since the data is stored in vectors that are frequently re-used, the data being held has both temporal and spatial locality. When the data pool runs out of space, it simply creates a new larger vector and appends it to the list of pools. The wait time is bounded by the number of nodes allocated (except for a CAS loop used to append a new array to the pool list for pushing). Benchmarks are still needed for this data structure.

Concurrent Map
-----------------

Concurrent Map is a concurrent container that stores values using an associated key, similar to std::map. It uses a simple hash (buckets) to partition keys before storing them in an AVL tree to handle collisions. Thread-safety is done through locking individual buckets as they are being used. The following methods are supported,
* void insert(KEY_TYPE key, T value)
* bool try_at(KEY_TYPE key, T& return_value)
* bool try_erase(KEY_TYPE key)

Below is an example of using ccl::map to add, read, and erase a value using a key.

```c++
#include "ccl.hpp"
#include <iostream>
#include <string>

int main() {
    ccl::map<int, std::string> map;
    map.insert(8, "testing");

    std::string value = "empty";
    if(map.try_at(8, value)) {
        std::cout << "Value read: " << value << std::endl;
    }

    map.try_erase(8);
}
```

Progress
-----------------

There is still a lot of work to do, for example, the classes are not properly setup (missing things like rule of 5 and std::move where applicable) and are not exception-safe. Finally, there is a lot of commenting, more than is normal for a project, because of how difficult concurrent programming can be. Feedback is appreciated, especially since, as stated previously, this is a naive implementation of thread-safe containers.
