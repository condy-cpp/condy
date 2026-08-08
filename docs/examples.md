# Examples

@brief Practical Condy code samples.

- [custom-allocator.cpp](custom-allocator_8cpp.html)
    Demonstrates the use of `condy::pmr` and custom memory allocators to improve the performance of task creation and destruction.

- [echo-server.cpp](echo-server_8cpp.html)
    A high-concurrency TCP echo server utilizing features like fixed file descriptors.

- [file-server.cpp](file-server_8cpp.html)
    A simple HTTP file server using `condy::async_splice` for asynchronous file and network IO.

- [fuse-prime-fs.cpp](fuse-prime-fs_8cpp.html)
    A FUSE file system server using `condy::async_uring_cmd` for FUSE-over-io_uring. It serves a dynamic tree over the integers 2..n: prime numbers are regular files holding their own value, and composite numbers are directories recursively containing all smaller numbers.

- [link-cp.cpp](link-cp_8cpp.html)
    Implements concurrent file copying using features like fixed file descriptors, fixed buffers, and link operations, supporting `O_DIRECT` IO. Achieves up to 2x performance improvement compared to `cp`.

- [queue-condy-futex.cpp](queue-condy-futex_8cpp.html)
    Builds a producer-consumer queue through `condy::Futex`, implementing asynchronous mutex and condition variable for synchronization.

- [queue-kernel-futex.cpp](queue-kernel-futex_8cpp.html)
    Builds a producer-consumer queue through asynchronous futex syscalls (`condy::async_futex_wait()`), implementing an asynchronous semaphore for synchronization.

- [ublk-loop.cpp](ublk-loop_8cpp.html)
    A ublk loop block device server, using condy coroutines to express the fetch -> perform I/O -> commit loop as straight-line code.

- [module-hello.cpp](module-hello_8cpp.html)
    Demonstrates using Condy as a C++20 module via `import condy;`. Requires CMake 3.28+, Ninja, and GCC 14+ or Clang 16+. Build with `-DCONDY_BUILD_MODULE=ON`.
