#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <condy.hpp>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <format>
#include <getopt.h>
#include <iostream>
#include <limits>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#if CONDY_URING_VERSION_GE(2, 4) // >= 2.4

size_t num_threads = 32;
size_t packet_size = 128;
size_t duration_seconds = 4;
size_t num_buffers = 64;
size_t sq_size = 256;
size_t cq_size = 2048;

void usage(const char *prog_name) {
    std::cerr << std::format(
        "Usage: {} [-h] [-t num_threads] [-p packet_size] "
        "[-d duration_seconds] [-b num_buffers] [-s sq_size] "
        "[-c cq_size]\n",
        prog_name);
}

condy::Coro<void> condy_multishot_session(int clientfd, size_t &total_nobufs) {
    condy::ProvidedBufferPool pool(num_buffers, packet_size);
    while (true) {
        auto [r, buf] = co_await condy::async_recv_multishot(clientfd, pool, 0,
                                                             [](auto &&) {});
        if (r == 0) {
            break;
        } else if (r == -ENOBUFS) {
            total_nobufs++;
            continue;
        } else if (r < 0) {
            std::cerr << std::format("recv error: {}\n", std::strerror(-r));
            exit(1);
        }
    }
    close(clientfd);
}

condy::Coro<void> condy_singleshot_session(int clientfd) {
    std::vector<char> buf(packet_size);
    while (true) {
        int r = co_await condy::async_recv(clientfd, condy::buffer(buf), 0);
        if (r == 0) {
            break;
        } else if (r < 0) {
            std::cerr << std::format("recv error: {}\n", std::strerror(-r));
            exit(1);
        }
    }
    close(clientfd);
}

void condy_multishot_server(int sockfd) {
    condy::RuntimeOptions options;
    options.event_interval(std::numeric_limits<size_t>::max());
    options.sq_size(sq_size);
    options.cq_size(cq_size);
    condy::Runtime runtime(options);
    size_t total_nobufs = 0;
    for (size_t i = 0; i < num_threads; ++i) {
        int clientfd = accept(sockfd, nullptr, nullptr);
        if (clientfd < 0) {
            std::perror("accept");
            exit(1);
        }
        condy::co_spawn(runtime,
                        condy_multishot_session(clientfd, total_nobufs))
            .detach();
    }
    runtime.allow_exit();
    runtime.run();
    std::cout << std::format("Condy ENOBUFS: {}\n", total_nobufs);
}

void condy_singleshot_server(int sockfd) {
    condy::RuntimeOptions options;
    options.event_interval(std::numeric_limits<size_t>::max());
    options.sq_size(sq_size);
    options.cq_size(cq_size);
    condy::Runtime runtime(options);
    for (size_t i = 0; i < num_threads; ++i) {
        int clientfd = accept(sockfd, nullptr, nullptr);
        if (clientfd < 0) {
            std::perror("accept");
            exit(1);
        }
        condy::co_spawn(runtime, condy_singleshot_session(clientfd)).detach();
    }
    runtime.allow_exit();
    runtime.run();
}

io_uring_sqe *get_sqe(io_uring &ring) {
    do {
        io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        if (sqe) {
            return sqe;
        }
        io_uring_submit(&ring);
    } while (true);
}

void raw_singleshot_server(int sockfd) {
    std::vector<int> client_fds;
    for (size_t i = 0; i < num_threads; ++i) {
        int clientfd = accept(sockfd, nullptr, nullptr);
        if (clientfd < 0) {
            std::perror("accept");
            exit(1);
        }
        client_fds.push_back(clientfd);
    }

    io_uring ring;
    io_uring_params params{};
    params.flags =
        IORING_SETUP_CLAMP | IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_CQSIZE;
    params.cq_entries = static_cast<uint32_t>(cq_size);
    io_uring_queue_init_params(sq_size, &ring, &params);
    io_uring_register_ring_fd(&ring);

    std::vector buffers(client_fds.size(), std::vector<char>(packet_size));

    auto add_recv = [&](size_t i) {
        io_uring_sqe *sqe = get_sqe(ring);
        io_uring_prep_recv(sqe, client_fds[i], buffers[i].data(), packet_size,
                           0);
        io_uring_sqe_set_data64(sqe, i);
    };

    for (size_t i = 0; i < client_fds.size(); ++i) {
        add_recv(i);
    }

    size_t pending_works = client_fds.size();
    while (pending_works > 0) {
        int r = io_uring_submit_and_wait(&ring, 1);
        if (r < 0) {
            std::cerr << std::format("io_uring_submit_and_wait: {}\n",
                                     std::strerror(-r));
            exit(1);
        }

        io_uring_cqe *cqe;
        unsigned head;
        size_t reaped_count = 0;
        io_uring_for_each_cqe(&ring, head, cqe) {
            if (cqe->res == 0) {
                --pending_works;
            } else if (cqe->res < 0) {
                std::cerr << std::format("recv error: {}\n",
                                         std::strerror(-cqe->res));
                exit(1);
            } else {
                size_t i = io_uring_cqe_get_data64(cqe);
                add_recv(i);
            }
            reaped_count++;
        }
        io_uring_cq_advance(&ring, reaped_count);
    }

    for (auto fd : client_fds) {
        close(fd);
    }

    io_uring_queue_exit(&ring);
}

void raw_multishot_server(int sockfd) {
    std::vector<int> client_fds;
    for (size_t i = 0; i < num_threads; ++i) {
        int clientfd = accept(sockfd, nullptr, nullptr);
        if (clientfd < 0) {
            std::perror("accept");
            exit(1);
        }
        client_fds.push_back(clientfd);
    }

    io_uring ring;
    io_uring_params params{};
    params.flags =
        IORING_SETUP_CLAMP | IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_CQSIZE;
    params.cq_entries = static_cast<uint32_t>(cq_size);
    io_uring_queue_init_params(sq_size, &ring, &params);
    io_uring_register_ring_fd(&ring);

    const int mask = io_uring_buf_ring_mask(num_buffers);
    std::vector<io_uring_buf_ring *> buf_rings(client_fds.size(), nullptr);
    std::vector<std::vector<char>> backing_buffers(
        client_fds.size(), std::vector<char>(num_buffers * packet_size));

    for (size_t i = 0; i < client_fds.size(); ++i) {
        const int bgid = static_cast<int>(i + 1);
        int setup_ret = 0;
        io_uring_buf_ring *br =
            io_uring_setup_buf_ring(&ring, num_buffers, bgid, 0, &setup_ret);
        if (!br) {
            std::cerr << std::format("io_uring_setup_buf_ring: {}\n",
                                     std::strerror(-setup_ret));
            exit(1);
        }
        buf_rings[i] = br;

        char *ptr = backing_buffers[i].data();
        for (size_t bid = 0; bid < num_buffers; ++bid) {
            io_uring_buf_ring_add(br, ptr, packet_size, static_cast<int>(bid),
                                  mask, static_cast<int>(bid));
            ptr += packet_size;
        }
        io_uring_buf_ring_advance(br, static_cast<int>(num_buffers));
    }

    auto add_recv_multishot = [&](size_t i) {
        io_uring_sqe *sqe = get_sqe(ring);
        io_uring_prep_recv_multishot(sqe, client_fds[i], nullptr, 0, 0);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = static_cast<uint16_t>(i + 1);
        io_uring_sqe_set_data64(sqe, i);
    };

    for (size_t i = 0; i < client_fds.size(); ++i) {
        add_recv_multishot(i);
    }

    size_t total_nobufs = 0;

    size_t pending_works = client_fds.size();
    while (pending_works > 0) {
        int r = io_uring_submit_and_wait(&ring, 1);
        if (r < 0) {
            std::cerr << std::format("io_uring_submit_and_wait: {}\n",
                                     std::strerror(-r));
            exit(1);
        }

        io_uring_cqe *cqe;
        unsigned head;
        size_t reaped_count = 0;
        io_uring_for_each_cqe(&ring, head, cqe) {
            const size_t i = io_uring_cqe_get_data64(cqe);
            if (cqe->res == 0) {
                --pending_works;
            } else if (cqe->res == -ENOBUFS) {
                ++total_nobufs;
                add_recv_multishot(i);
            } else if (cqe->res < 0) {
                std::cerr << std::format("recv: {}\n",
                                         std::strerror(-cqe->res));
                exit(1);
            } else {
                assert(cqe->flags & IORING_CQE_F_BUFFER);
                const int bid =
                    static_cast<int>(cqe->flags >> IORING_CQE_BUFFER_SHIFT);
                char *buffer_ptr =
                    backing_buffers[i].data() + bid * packet_size;
                io_uring_buf_ring_add(buf_rings[i], buffer_ptr, packet_size,
                                      bid, mask, 0);
                io_uring_buf_ring_advance(buf_rings[i], 1);

                if (!(cqe->flags & IORING_CQE_F_MORE)) {
                    add_recv_multishot(i);
                }
            }

            ++reaped_count;
        }
        io_uring_cq_advance(&ring, reaped_count);
    }

    for (size_t i = 0; i < client_fds.size(); ++i) {
        io_uring_free_buf_ring(&ring, buf_rings[i],
                               static_cast<int>(num_buffers),
                               static_cast<int>(i + 1));
        close(client_fds[i]);
    }

    io_uring_queue_exit(&ring);

    std::cout << std::format("Raw ENOBUFS: {}\n", total_nobufs);
}

int prepare_server_fd(const sockaddr_in &addr) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::perror("socket");
        exit(1);
    }
    if (bind(sockfd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) <
        0) {
        std::perror("bind");
        exit(1);
    }
    if (listen(sockfd, 128) < 0) {
        std::perror("listen");
        exit(1);
    }
    return sockfd;
}

void stress_thread(const sockaddr_in &addr, std::atomic<size_t> &total_packets,
                   std::atomic<bool> &running) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::perror("socket");
        exit(1);
    }
    if (connect(sockfd, reinterpret_cast<const sockaddr *>(&addr),
                sizeof(addr)) < 0) {
        std::perror("connect");
        close(sockfd);
        exit(1);
    }
    std::vector<char> buf(packet_size, 'x');
    size_t count = 0;
    while (running.load(std::memory_order_relaxed)) {
        size_t sent = 0;
        while (sent < buf.size()) {
            ssize_t r = send(sockfd, buf.data() + sent, buf.size() - sent, 0);
            if (r < 0) {
                std::cerr << std::format("send error: {}\n",
                                         std::strerror(errno));
                exit(1);
            }
            sent += r;
        }
        count++;
    }
    close(sockfd);
    total_packets.fetch_add(count, std::memory_order_relaxed);
}

struct Result {
    std::chrono::duration<double> duration;
    size_t total_packets;
};

Result stress(const sockaddr_in &addr) {
    std::atomic<size_t> total_packets{0};
    std::atomic<bool> running{true};

    std::this_thread::sleep_for(
        std::chrono::milliseconds(10)); // Wait for the server to be ready

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        threads.emplace_back(stress_thread, addr, std::ref(total_packets),
                             std::ref(running));
    }

    std::this_thread::sleep_for(std::chrono::seconds(duration_seconds));
    running.store(false, std::memory_order_relaxed);

    for (auto &t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();

    auto duration = end - start;
    return {duration, total_packets.load(std::memory_order_relaxed)};
}

template <typename Fn> auto run(Fn &&server_func) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    addr.sin_port = 0; // Let the OS choose the port

    int server_fd = prepare_server_fd(addr);

    socklen_t len = sizeof(addr);
    if (getsockname(server_fd, reinterpret_cast<sockaddr *>(&addr), &len) < 0) {
        std::perror("getsockname");
        exit(1);
    }

    std::thread server_thread(std::forward<Fn>(server_func), server_fd);
    auto result = stress(addr);
    server_thread.join();

    close(server_fd);

    return result;
}

int main(int argc, char **argv) noexcept(false) {
    int opt;
    while ((opt = getopt(argc, argv, "ht:p:d:b:s:c:")) != -1) {
        switch (opt) {
        case 't':
            num_threads = std::stoul(optarg);
            break;
        case 'p':
            packet_size = std::stoul(optarg);
            break;
        case 'd':
            duration_seconds = std::stoul(optarg);
            break;
        case 'b':
            num_buffers = std::bit_ceil(std::stoul(optarg));
            break;
        case 's':
            sq_size = std::stoul(optarg);
            break;
        case 'c':
            cq_size = std::stoul(optarg);
            break;
        case 'h':
            usage(argv[0]);
            return 0;
        default:
            usage(argv[0]);
            return 1;
        }
    }

    if (optind != argc) {
        usage(argv[0]);
        return 1;
    }

    std::cout << std::format(
        "recv config: num_threads={}, packet_size={}, duration_seconds={}, "
        "num_buffers={}, sq_size={}, cq_size={}\n",
        num_threads, packet_size, duration_seconds, num_buffers, sq_size,
        cq_size);

    std::cout << "Running Condy singleshot recv benchmark...\n";
    auto condy_singleshot_result = run(condy_singleshot_server);
    auto condy_singleshot_throughput =
        static_cast<double>(condy_singleshot_result.total_packets) /
        condy_singleshot_result.duration.count();
    std::cout << std::format(
        "Condy singleshot recv: Received {} packets in {:.2f} seconds "
        "({:.2f} packets/second)\n",
        condy_singleshot_result.total_packets,
        condy_singleshot_result.duration.count(), condy_singleshot_throughput);

    std::cout << "Running Condy multishot recv benchmark...\n";
    auto condy_multishot_result = run(condy_multishot_server);
    auto condy_multishot_throughput =
        static_cast<double>(condy_multishot_result.total_packets) /
        condy_multishot_result.duration.count();
    std::cout << std::format(
        "Condy multishot recv: Received {} packets in {:.2f} seconds "
        "({:.2f} packets/second)\n",
        condy_multishot_result.total_packets,
        condy_multishot_result.duration.count(), condy_multishot_throughput);

    std::cout << "Running raw singleshot recv benchmark...\n";
    auto raw_singleshot_result = run(raw_singleshot_server);
    auto raw_singleshot_throughput =
        static_cast<double>(raw_singleshot_result.total_packets) /
        raw_singleshot_result.duration.count();
    std::cout << std::format(
        "Raw singleshot recv: Received {} packets in {:.2f} seconds "
        "({:.2f} packets/second)\n",
        raw_singleshot_result.total_packets,
        raw_singleshot_result.duration.count(), raw_singleshot_throughput);

    std::cout << "Running raw multishot recv benchmark...\n";
    auto raw_multishot_result = run(raw_multishot_server);
    auto raw_multishot_throughput =
        static_cast<double>(raw_multishot_result.total_packets) /
        raw_multishot_result.duration.count();
    std::cout << std::format(
        "Raw multishot recv: Received {} packets in {:.2f} seconds "
        "({:.2f} packets/second)\n",
        raw_multishot_result.total_packets,
        raw_multishot_result.duration.count(), raw_multishot_throughput);

    auto condy_singleshot_time_per_op =
        condy_singleshot_result.duration.count() /
        static_cast<double>(condy_singleshot_result.total_packets);
    auto raw_singleshot_time_per_op =
        raw_singleshot_result.duration.count() /
        static_cast<double>(raw_singleshot_result.total_packets);
    auto condy_multishot_time_per_op =
        condy_multishot_result.duration.count() /
        static_cast<double>(condy_multishot_result.total_packets);
    auto raw_multishot_time_per_op =
        raw_multishot_result.duration.count() /
        static_cast<double>(raw_multishot_result.total_packets);

    auto condy_singleshot_overhead =
        (condy_singleshot_time_per_op - raw_singleshot_time_per_op) /
        raw_singleshot_time_per_op * 100.0;
    auto condy_multishot_overhead =
        (condy_multishot_time_per_op - raw_multishot_time_per_op) /
        raw_multishot_time_per_op * 100.0;
    std::cout << std::format(
        "Condy singleshot recv overhead compared to raw singleshot recv: "
        "{:.2f}%\n",
        condy_singleshot_overhead);
    std::cout << std::format(
        "Condy multishot recv overhead compared to raw multishot recv: "
        "{:.2f}%\n",
        condy_multishot_overhead);

    return 0;
}

#else

int main() {
    std::cerr << "This benchmark requires io_uring version 2.4 or later\n";
    return 0;
}

#endif