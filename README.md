Concurrent Containers Library
=================

The Concurrent Containers Library (CCL) is a personal attempt to create concurrent containers that are both thread-safe and performant. This means that, where possible, they are implemented as lock-free. Additionally, the interfaces for each container is different than the comparable standard library version. It should be noted that this is a naive implementation of concurrent containers, and should not be used for a serious project (there are no guarantees that it is thread-safe, despite having been tested).

Concurrent Stack
-----------------

Concurrent Stack is a lock-free LIFO stack that supports the following methods,
* bool try_pop(T& value)
* void push(value)
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

Concurrent Queue is a lock-free FIFO singly-linked list that supports the following methods,
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

Concurrent Vector
-----------------

Concurrent Vector is an array based container that supports the following methods,
* bool try_at(std::size_t index, T& value)
* void push_back(T value)
* bool try_insert(std::size_t position, T value)
* std::size_t size()
* void clear()
* bool try_erase(std::size_t position)
* bool test_and_erase(std::size_t position, T value)
* iterator begin()
* iterator end()

```c++
#include "ccl.hpp"
#include <iostream>

int main() {
    std::cout << "Starting concurrent vector test" << std::endl;
    ccl::vector<std::string> vector;

    for (std::size_t index = 0; index < 10; ++index) {
        vector.push_back("value_" + std::to_string(index));
    }

    unsigned int index = 0;
    for (auto entry : vector) {
        if (entry) {
            std::cout << "For index: " << index << " result is: " << *entry << std::endl;
        } else {
            std::cout << "For index: " << index << " result does not exist." << std::endl;
        };

        ++index;
    }
}
```

Concurrent vector uses locks for writes but is lock-free and wait-free for reads, which helps if your program only seldom writes to the vector.

Progress
-----------------

There is still a lot of work to do and not every method works properly (I believe vector currently has an issue with inserting values). Additionally, the classes are not properly setup (missing things like rule of 5 and std::move where applicable). Finally, there is a lot of commenting, more than is normal for a project, because of how difficult concurrent programming can be. Feedback is appreciated.
