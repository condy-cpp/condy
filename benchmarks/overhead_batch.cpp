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

void usage(const char *prog_name) {
    std::cerr << std::format(
        "Usage: {} [-h] [-s sq_entries] [-t task_count] [-p nops_per_task]\n",
        prog_name);
}

condy::Coro<void> nop_task() {
    for (size_t i = 0; i < nops_per_task; ++i) {
        co_await condy::async_nop();
    }
}

condy::Coro<void> run_batched_nop() {
    std::vector<condy::Task<void>> tasks;
    tasks.reserve(task_count);

    for (size_t i = 0; i < task_count; ++i) {
        tasks.emplace_back(condy::co_spawn(nop_task()));
    }

    for (auto &task : tasks) {
        co_await std::move(task);
    }
}

int main(int argc, char **argv) noexcept(false) {
    int opt;
    while ((opt = getopt(argc, argv, "hs:t:p:")) != -1) {
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

    const size_t total_operations = task_count * nops_per_task;

    condy::Runtime runtime(condy::RuntimeOptions{}.sq_size(sq_entries));

    const auto start = std::chrono::high_resolution_clock::now();
    condy::sync_wait(runtime, run_batched_nop());
    const auto end = std::chrono::high_resolution_clock::now();

    const auto duration_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();
    const double ns_per_op = static_cast<double>(duration_ns) /
                             static_cast<double>(total_operations);

    std::cout << std::format("overhead_batch config: sq_entries={}, "
                             "task_count={}, nops_per_task={}\n",
                             sq_entries, task_count, nops_per_task);
    std::cout << std::format("Total operations: {}\n", total_operations);
    std::cout << std::format("Total time: {} ns\n", duration_ns);
    std::cout << std::format("ns per op: {:.2f}\n", ns_per_op);

    return 0;
}