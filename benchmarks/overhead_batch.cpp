#include <chrono>
#include <condy.hpp>
#include <cstddef>
#include <format>
#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

size_t sq_entries = 256;
size_t task_count = 256;
size_t nops_per_task = 10'000;
bool enable_sqpoll = false;
size_t submit_batch = 0;

void usage(const char *prog_name) {
    std::cerr << std::format("Usage: {} [-hq] [-s sq_entries] [-t task_count] "
                             "[-p nops_per_task] [-b submit_batch]\n",
                             prog_name);
}

io_uring_sqe *get_sqe(io_uring &ring, size_t &pending_batch_count) noexcept {
    io_uring_sqe *sqe;
    do {
        sqe = io_uring_get_sqe(&ring);
        if (sqe) {
            break;
        }
        io_uring_submit(&ring);
        pending_batch_count = 0;
        if (ring.flags & IORING_SETUP_SQPOLL) {
            io_uring_sqring_wait(&ring);
        }
    } while (true);
    return sqe;
}

void run_raw_nop_batch() {
    io_uring_params params = {};
    params.flags |= IORING_SETUP_CLAMP;
    params.flags |= IORING_SETUP_SINGLE_ISSUER;
    if (enable_sqpoll) {
        params.flags |= IORING_SETUP_SQPOLL;
        params.sq_thread_idle = 1000; // 1 second
    }
    if (submit_batch == 0) {
        if (enable_sqpoll) {
            submit_batch = std::min<size_t>(32, sq_entries);
        } else {
            submit_batch = std::numeric_limits<size_t>::max();
        }
    }
    io_uring ring;
    io_uring_queue_init_params(sq_entries, &ring, &params);
    io_uring_register_ring_fd(&ring);

    std::vector<size_t> completed_nops_per_task(task_count, 0);
    size_t remaining_tasks = task_count;
    size_t pending_batch_count = 0;

    for (size_t i = 0; i < task_count; ++i) {
        io_uring_sqe *sqe = get_sqe(ring, pending_batch_count);
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data64(sqe, i);
        pending_batch_count++;
        if (pending_batch_count == submit_batch) {
            io_uring_submit(&ring);
            pending_batch_count = 0;
        }
    }

    while (remaining_tasks > 0) {
        io_uring_cqe *cqe;
        unsigned head;
        size_t reaped_count = 0;
        io_uring_submit_and_wait(&ring, 1);
        io_uring_for_each_cqe(&ring, head, cqe) {
            size_t task_index = io_uring_cqe_get_data64(cqe);
            completed_nops_per_task[task_index]++;
            if (completed_nops_per_task[task_index] == nops_per_task) {
                remaining_tasks--;
            } else {
                io_uring_sqe *sqe = get_sqe(ring, pending_batch_count);
                io_uring_prep_nop(sqe);
                io_uring_sqe_set_data64(sqe, task_index);
                pending_batch_count++;
                if (pending_batch_count == submit_batch) {
                    io_uring_submit(&ring);
                    pending_batch_count = 0;
                }
            }
            reaped_count++;
        }
        io_uring_cq_advance(&ring, reaped_count);
    }

    io_uring_queue_exit(&ring);
}

condy::Coro<void> nop_worker() {
    for (size_t i = 0; i < nops_per_task; ++i) {
        co_await condy::async_nop();
    }
}

condy::Coro<void> run_condy_nop_batch_coro() {
    std::vector<condy::Task<void>> tasks;
    tasks.reserve(task_count);

    for (size_t i = 0; i < task_count; ++i) {
        tasks.emplace_back(condy::co_spawn(nop_worker()));
    }

    for (auto &task : tasks) {
        co_await std::move(task);
    }
}

void run_condy_nop_batch() {
    condy::RuntimeOptions options;
    options.sq_size(sq_entries);
    options.submit_batch(submit_batch);
    if (enable_sqpoll) {
        options.enable_sqpoll(1000);
    }
    condy::Runtime runtime(options);
    condy::sync_wait(runtime, run_condy_nop_batch_coro());
}

int main(int argc, char **argv) noexcept(false) {
    int opt;
    while ((opt = getopt(argc, argv, "hs:t:p:qb:")) != -1) {
        switch (opt) {
        case 's':
            sq_entries = std::stoul(optarg);
            break;
        case 't':
            task_count = std::stoul(optarg);
            break;
        case 'p':
            nops_per_task = std::stoul(optarg);
            break;
        case 'q':
            enable_sqpoll = true;
            break;
        case 'b':
            submit_batch = std::stoul(optarg);
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
        "overhead_batch config: sq_entries={}, "
        "task_count={}, nops_per_task={}, enable_sqpoll={}, submit_batch={}\n",
        sq_entries, task_count, nops_per_task, enable_sqpoll, submit_batch);

    const size_t total_operations = task_count * nops_per_task;

    long duration_raw_ns = 0;
    long duration_condy_ns = 0;

    {
        auto start = std::chrono::high_resolution_clock::now();
        run_condy_nop_batch();
        auto end = std::chrono::high_resolution_clock::now();
        duration_condy_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count();
        std::cout << std::format(
            "Condy NOP Batch: {} operations took {} ns ({} ns per op)\n",
            total_operations, duration_condy_ns,
            duration_condy_ns / static_cast<long>(total_operations));
    }

    {
        auto start = std::chrono::high_resolution_clock::now();
        run_raw_nop_batch();
        auto end = std::chrono::high_resolution_clock::now();
        duration_raw_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count();
        std::cout << std::format(
            "Raw NOP Batch: {} operations took {} ns ({} ns per op)\n",
            total_operations, duration_raw_ns,
            duration_raw_ns / static_cast<long>(total_operations));
    }

    long overhead_ns = duration_condy_ns - duration_raw_ns;
    long overhead_per_op = overhead_ns / static_cast<long>(total_operations);
    std::cout << std::format("Overhead: {} ns per operation ({:.2f}%)\n",
                             overhead_per_op,
                             (static_cast<double>(overhead_ns) /
                              static_cast<double>(duration_raw_ns)) *
                                 100.0);

    return 0;
}