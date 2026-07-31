/**
 * @file ublk-loop.cpp
 * @brief ublk loop block device server using condy
 * @details This example demonstrates how condy's coroutine model simplifies
 * writing a ublk server: the fetch -> perform I/O -> commit request/response
 * loop becomes straight-line code, with one `io_loop()` coroutine per tag
 * awaiting `async_uring_cmd` for fetch/commit and `async_read`/`async_write`/
 * `async_fsync` for the backing file.
 */

#include <condy.hpp>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <iostream>
#include <linux/ublk_cmd.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <vector>

size_t num_queues = 1;
size_t queue_depth = 32;
uint32_t dev_id = -1;

constexpr auto *CTRL_FILE = "/dev/ublk-control";
constexpr size_t SECTOR_SIZE = 512;
constexpr size_t MAX_SECTORS = 128;
constexpr size_t BUF_SIZE = MAX_SECTORS * SECTOR_SIZE;
constexpr size_t BS_SHIFT = 9;
constexpr int FIXED_FD = 0;

std::vector<std::unique_ptr<condy::Runtime>> queue_runtimes;
size_t dev_sectors;
int backing_fd;
int signal_fd;
int ctrl_fd;
int ublkc_fd;
void *bufs_base;

bool multi_queues() { return num_queues > 1; }

off_t io_desc_offset(size_t q_id) {
    return UBLKSRV_CMD_BUF_OFFSET +
           q_id * UBLK_MAX_QUEUE_DEPTH * sizeof(ublksrv_io_desc);
}

auto ublk_ctrl_cmd(int fd, int cmd_op, const ublksrv_ctrl_cmd &ctrl) {
    return condy::async_uring_cmd(cmd_op, fd, [&](io_uring_sqe *sqe) {
        std::memcpy(sqe->cmd, &ctrl, sizeof(ctrl));
    });
}

auto ublk_io_cmd(int cmd_op, const ublksrv_io_cmd &cmd) {
    return condy::async_uring_cmd(
        cmd_op, condy::fixed(FIXED_FD),
        [&](io_uring_sqe *sqe) { std::memcpy(sqe->cmd, &cmd, sizeof(cmd)); });
}

condy::Coro<void> setup_device() {
    int r;
    ublksrv_ctrl_cmd ctrl;

    ctrl_fd = co_await condy::async_open(CTRL_FILE, O_RDWR, 0);
    if (ctrl_fd < 0) {
        std::cerr << std::format("Failed to open {}: {}\n", CTRL_FILE, ctrl_fd);
        std::exit(1);
    }

    ublksrv_ctrl_dev_info info = {};
    info.dev_id = dev_id;
    info.nr_hw_queues = num_queues;
    info.queue_depth = queue_depth;
    info.max_io_buf_bytes = BUF_SIZE;
    ctrl = {};
    ctrl.dev_id = info.dev_id;
    ctrl.queue_id = -1;
    ctrl.addr = reinterpret_cast<uint64_t>(&info);
    ctrl.len = sizeof(info);
    r = co_await ublk_ctrl_cmd(ctrl_fd, UBLK_U_CMD_ADD_DEV, ctrl);
    if (r < 0) {
        std::cerr << std::format("ADD_DEV failed: {}\n", r);
        std::exit(1);
    }
    dev_id = info.dev_id; // Get actual dev_id

    std::string ublkc_path = std::format("/dev/ublkc{}", dev_id);
    ublkc_fd = co_await condy::async_open(ublkc_path.c_str(), O_RDWR, 0);
    if (ublkc_fd < 0) {
        std::cerr << std::format("Failed to open {}: {}\n", ublkc_path,
                                 ublkc_fd);
        std::exit(1);
    }
    std::cout << std::format("ublk-loop: /dev/ublkc{} created\n", dev_id);

    ublk_params params = {};
    params.len = sizeof(params);
    params.types = UBLK_PARAM_TYPE_BASIC;
    params.basic.attrs = UBLK_ATTR_VOLATILE_CACHE;
    params.basic.logical_bs_shift = BS_SHIFT;
    params.basic.physical_bs_shift = BS_SHIFT;
    params.basic.io_opt_shift = BS_SHIFT;
    params.basic.io_min_shift = BS_SHIFT;
    params.basic.max_sectors = MAX_SECTORS;
    params.basic.dev_sectors = dev_sectors;
    ctrl = {};
    ctrl.dev_id = dev_id;
    ctrl.queue_id = -1;
    ctrl.addr = reinterpret_cast<uint64_t>(&params);
    ctrl.len = sizeof(params);
    r = co_await ublk_ctrl_cmd(ctrl_fd, UBLK_U_CMD_SET_PARAMS, ctrl);
    if (r < 0) {
        std::cerr << std::format("SET_PARAMS failed: {}\n", r);
        std::exit(1);
    }

    std::cout << std::format("ublk-loop: /dev/ublkc{} configured\n", dev_id);
}

condy::Coro<void> io_loop(size_t q_id, size_t tag,
                          ublksrv_io_desc *io_desc_base) {
    int r;
    ublksrv_io_cmd cmd;

    void *buf = static_cast<char *>(bufs_base) +
                q_id * (queue_depth * BUF_SIZE) + tag * BUF_SIZE;
    ublksrv_io_desc *io_desc = io_desc_base + tag;

    cmd = {};
    cmd.q_id = q_id;
    cmd.tag = tag;
    cmd.addr = reinterpret_cast<uint64_t>(buf);
    r = co_await ublk_io_cmd(UBLK_U_IO_FETCH_REQ, cmd);
    if (r == UBLK_IO_RES_ABORT) {
        co_return;
    } else if (r < 0) {
        std::cerr << std::format("FETCH_REQ failed: {}\n", r);
        exit(1);
    }

    while (true) {
        uint8_t op = ublksrv_get_op(io_desc);
        size_t start = io_desc->start_sector * SECTOR_SIZE;
        size_t size = io_desc->nr_sectors * SECTOR_SIZE;
        switch (op) {
        case UBLK_IO_OP_READ:
            r = co_await condy::async_read(backing_fd, condy::buffer(buf, size),
                                           start);
            if (r < 0) {
                std::cerr << std::format("async_read backing file failed: {}\n",
                                         r);
            }
            break;
        case UBLK_IO_OP_WRITE:
            r = co_await condy::async_write(backing_fd,
                                            condy::buffer(buf, size), start);
            if (r < 0) {
                std::cerr << std::format(
                    "async_write backing file failed: {}\n", r);
            }
            break;
        case UBLK_IO_OP_FLUSH:
            r = co_await condy::async_fsync(backing_fd, IORING_FSYNC_DATASYNC);
            if (r < 0) {
                std::cerr << std::format("async_fsync failed: {}\n", r);
            }
            break;
        default:
            std::cerr << std::format("Unknown op: {}\n", op);
            r = -EINVAL;
            break;
        }

        cmd = {};
        cmd.q_id = q_id;
        cmd.tag = tag;
        cmd.result = static_cast<int32_t>(r);
        cmd.addr = reinterpret_cast<uint64_t>(buf);
        r = co_await ublk_io_cmd(UBLK_U_IO_COMMIT_AND_FETCH_REQ, cmd);
        if (r == UBLK_IO_RES_ABORT) {
            co_return;
        } else if (r < 0) {
            std::cerr << std::format("COMMIT_AND_FETCH_REQ failed: {}\n", r);
            exit(1);
        }
    }
}

condy::Coro<void> io_queue(size_t q_id) {
    size_t io_desc_base_size = queue_depth * sizeof(ublksrv_io_desc);
    void *addr =
        mmap(nullptr, io_desc_base_size, PROT_READ, MAP_SHARED | MAP_POPULATE,
             ublkc_fd, io_desc_offset(q_id));
    if (addr == MAP_FAILED) {
        std::perror("mmap io_desc_base");
        std::exit(1);
    }
    auto *io_desc_base = reinterpret_cast<ublksrv_io_desc *>(addr);

    auto &fd_table = condy::current_runtime().fd_table();
    int r = fd_table.init(&ublkc_fd, 1);
    if (r < 0) {
        std::cerr << std::format("fd_table.init failed: {}\n", r);
        std::exit(1);
    }

    std::vector<condy::Task<void>> tasks;
    tasks.reserve(queue_depth);
    for (size_t tag = 0; tag < queue_depth; tag++) {
        tasks.push_back(condy::co_spawn(io_loop(q_id, tag, io_desc_base)));
    }
    for (auto &t : tasks) {
        co_await t;
    }

    fd_table.destroy();
    munmap(io_desc_base, io_desc_base_size);
}

condy::Coro<void> co_main() {
    int r;
    ublksrv_ctrl_cmd ctrl;

    co_await setup_device();

    bufs_base =
        mmap(nullptr, num_queues * queue_depth * BUF_SIZE,
             PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (bufs_base == MAP_FAILED) {
        std::perror("mmap bufs_base");
        std::exit(1);
    }

    std::vector<condy::Task<void>> tasks;

    if (multi_queues()) {
        tasks.reserve(num_queues);
        for (size_t q_id = 0; q_id < num_queues; q_id++) {
            tasks.push_back(
                condy::co_spawn(*queue_runtimes[q_id], io_queue(q_id)));
        }
    } else {
        tasks.push_back(condy::co_spawn(io_queue(0)));
    }

    ctrl = {};
    ctrl.dev_id = dev_id;
    ctrl.queue_id = -1;
    ctrl.data[0] = getpid();
    r = co_await ublk_ctrl_cmd(ctrl_fd, UBLK_U_CMD_START_DEV, ctrl);
    if (r < 0) {
        std::cerr << std::format("START_DEV failed: {}\n", r);
        std::exit(1);
    }
    std::cout << std::format("ublk-loop: /dev/ublkb{} started\n", dev_id);

    signalfd_siginfo si;
    co_await condy::async_read(signal_fd, condy::buffer(&si, sizeof(si)), 0);
    std::cout << std::format(
        "ublk-loop: received signal {}, shutting down...\n", si.ssi_signo);

    ctrl = {};
    ctrl.dev_id = dev_id;
    ctrl.queue_id = -1;
    r = co_await ublk_ctrl_cmd(ctrl_fd, UBLK_U_CMD_STOP_DEV, ctrl);
    if (r < 0) {
        std::cerr << std::format("STOP_DEV failed: {}\n", r);
        exit(1);
    }
    std::cout << "ublk-loop: device stopped\n";

    for (auto &t : tasks) {
        co_await t;
    }

    munmap(bufs_base, num_queues * queue_depth * BUF_SIZE);
    co_await condy::async_close(ublkc_fd);

    ctrl = {};
    ctrl.dev_id = dev_id;
    ctrl.queue_id = -1;
    r = co_await ublk_ctrl_cmd(ctrl_fd, UBLK_U_CMD_DEL_DEV, ctrl);
    if (r < 0) {
        std::cerr << std::format("DEL_DEV failed: {}\n", r);
    }
    std::cout << std::format("ublk-loop: device /dev/ublkb{} deleted\n",
                             dev_id);

    co_await condy::async_close(ctrl_fd);
}

void usage(const char *prog) {
    std::cerr << std::format("Usage: {} [OPTIONS] <backing-file>\n"
                             "Options:\n"
                             "  -d NUM    Queue depth (default: 32)\n"
                             "  -n NUM    Number of queues (default: 1)\n"
                             "  -i NUM    Device ID (default: auto)\n"
                             "  -h        Show this help\n",
                             prog);
}

int main(int argc, char *argv[]) noexcept(false) {
    int opt;
    while ((opt = getopt(argc, argv, "d:n:i:h")) != -1) {
        switch (opt) {
        case 'd':
            queue_depth = std::stoull(optarg);
            break;
        case 'n':
            num_queues = std::stoull(optarg);
            break;
        case 'i':
            dev_id = std::stoul(optarg);
            break;
        case 'h':
        default:
            usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }
    std::string backing_path = argv[optind];

    backing_fd = open(backing_path.c_str(), O_RDWR);
    if (backing_fd < 0) {
        std::perror("open backing file");
        return 1;
    }

    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigprocmask(SIG_BLOCK, &mask, nullptr);

    signal_fd = signalfd(-1, &mask, SFD_NONBLOCK);
    if (signal_fd < 0) {
        std::perror("signalfd");
        exit(1);
    }

    off_t file_size = lseek(backing_fd, 0, SEEK_END);
    dev_sectors = static_cast<size_t>(file_size) / SECTOR_SIZE;
    std::cout << std::format("ublk-loop: backing file {} MiB ({} sectors)\n",
                             file_size / 1024 / 1024, dev_sectors);

    condy::RuntimeOptions options;
    options.enable_sqe128();
    options.sq_size(multi_queues() ? 32 : queue_depth);
    condy::Runtime runtime(options);

    std::vector<std::thread> queue_threads;
    if (multi_queues()) {
        condy::RuntimeOptions queue_options;
        queue_options.enable_attach_wq(runtime);
        queue_options.sq_size(queue_depth);

        queue_runtimes.reserve(num_queues);
        queue_threads.reserve(num_queues);
        for (size_t i = 0; i < num_queues; i++) {
            queue_runtimes.push_back(
                std::make_unique<condy::Runtime>(queue_options));
            queue_threads.emplace_back([&, i] { queue_runtimes[i]->run(); });
        }
    }

    condy::sync_wait(runtime, co_main());

    for (size_t i = 0; i < queue_runtimes.size(); i++) {
        queue_runtimes[i]->allow_exit();
        queue_threads[i].join();
    }

    close(signal_fd);
    close(backing_fd);
    return 0;
}