# Benchmarks

@brief Performance comparison between Condy and other frameworks.

The benchmark source code can be found at [condy-bench](https://github.com/condy-cpp/condy-bench).

Test Environment:
- **CPU**: AMD Ryzen 9 7945HX with Radeon Graphics × 16
- **Storage**: SK Hynix HFS001TEJ9X115N (NVMe SSD, 1TB, PCIe 4.0 x4)
- **Compiler**: clang version 18.1.3
- **OS**: Linux Mint 22 Cinnamon
- **Kernel**: 7.0.0-14-generic

Baselines:
- **libaio**: The asynchronous file IO framework on Linux before io_uring.
- **[liburing](https://github.com/axboe/liburing)**: io_uring itself. Since it is not wrapped with coroutines, it is slightly less convenient to use.
- **[Asio](https://github.com/chriskohlhoff/asio)**: A popular IO framework in C++.
- **[Monoio](https://github.com/bytedance/monoio)**: A Rust coroutine framework based on io_uring.
- **[Compio](https://github.com/compio-rs/compio)**: Another Rust coroutine framework based on io_uring.

## Random File Read

We tested the performance of Condy in 4KB random reads on an 8GB file, collected IOPS data, and compared it with baseline implementations using libaio and liburing.

As shown in the figure, as the queue depth (number of concurrent tasks) increases, the read IOPS gradually rises. Condy without Direct IO saturates around 215K IOPS after QD=64, while Direct IO variants continue to scale linearly. At QD=128, Condy (Fixed + Direct + IOPoll) reaches **598K IOPS**, nearly identical to Uring (Fixed + Direct + IOPoll) at 601K IOPS.

Aio saturates at approximately 200K IOPS after QD=32 and shows no further improvement. Under the same Direct IO configuration, Condy reaches 345K IOPS at QD=128, outperforming Monoio (310K, **1.11x**) and Compio (297K, **1.16x**). With further optimizations (fixed files, registered buffers, and IO Polling), Condy's best configuration achieves 598K IOPS — roughly **2x** ahead of Monoio and Compio, and a **3x** improvement over Aio.

Similarly, Condy's performance is roughly the same as the baseline program implemented with liburing under the same configuration.

<div align="center">
  <img src="file_random_read_queue_depth.png" width="60%">
</div>

## Network Receive Throughput

We tested the single-machine TCP receive throughput of Condy, Raw io_uring, Asio, Epoll, Monoio, and Compio under varying message sizes, covering both single-shot and multi-shot modes. While Monoio does not support multi-shot recv, Compio provides built-in multi-shot support.

<div align="center">
  <img src="recv_message_size.png" width="60%">
</div>

As shown in the figure, throughput scales linearly with message size across all implementations. In multi-shot mode, Condy achieves **2610 MB/s** at 2048B, nearly identical to Raw io_uring (2560 MB/s) and slightly ahead of Compio multi-shot (2411 MB/s, **1.08×**). All three share a similar pooled buffer design, and Condy's io_uring abstraction introduces virtually no overhead.

In the single-shot group, Condy reaches **2110 MB/s**, outperforming Compio (1758 MB/s, **1.20×**) and Monoio (1887 MB/s, **1.12×**) by clear margins. Asio (1588 MB/s) and Epoll (1734 MB/s) lag further behind due to the lack of io_uring support.

The multi-shot advantage over single-shot is also pronounced: Condy multi-shot improves upon its single-shot baseline by **1.24×**, while Compio multi-shot improves by **1.37×**.

## Channel

We compared the performance of Condy, Asio, Monoio, and Compio Channels by varying the number of messages sent and the number of Channels, and measuring the total time taken. (Monoio and Compio use `futures::channel::mpsc`.)

<div align="center">
  <img src="channel_number_of_messages.png" width="60%">
</div>

<div align="center">
  <img src="channel_task_pairs.png" width="60%">
</div>

As shown in the figures, as the number of messages and concurrent tasks increases, the total time for these frameworks increases linearly. In terms of execution time, Condy achieves a **15x** performance improvement over Asio (2M messages: 39ms vs 598ms; 32 task pairs: 626ms vs 9563ms). Compared to Monoio and Compio, there is also a **1.2x** performance improvement.

## Coroutine Spawn

We compared the efficiency of Condy, Asio, Monoio, and Compio in coroutine creation by varying the number of coroutines created and measuring the total time taken.

<div align="center">
  <img src="spawn_number_of_tasks.png" width="60%">
</div>

As shown in the figure, as the number of coroutines increases, the total time for these frameworks increases linearly. In terms of execution time, Condy achieves a **6.6x** performance improvement over Asio (4M tasks: 236ms vs 1566ms) and a **7.5x** improvement over Compio (236ms vs 1774ms). Condy's performance is on par with Monoio (236ms vs 242ms). The average creation time per coroutine for Condy is 56ns (compared to 58ns for Monoio and 373ns for Asio).

## Coroutine Switch

We compared the efficiency of Condy, Asio, and Monoio in coroutine context switching by repeatedly switching coroutines and measuring the total time taken. (Monoio use `futures_lite::future::yield_now`. Compio was not included in this comparison due to excessive time consumption in this test.)

<div align="center">
  <img src="post_switch_times.png" width="60%">
</div>

As shown in the figure, as the number of switches increases, the total time for these frameworks increases linearly. In terms of execution time, Condy achieves a **16x** performance improvement over Asio (8M switches: 28ms vs 449ms). It also achieves performance results close to Monoio (28ms vs 27ms), with the difference per switch not exceeding 0.2ns (about 3.3ns per switch).