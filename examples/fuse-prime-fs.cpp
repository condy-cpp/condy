/**
 * @file fuse-prime-fs.cpp
 * @brief FUSE prime file system server using condy.
 * @details This fs is inspired by
 * https://github.com/hanwen/go-fuse/blob/master/fs/dynamic_example_test.go
 *
 * It builds a dynamic tree over the integers 2..n (n is given by -n, default
 * 10). The root is always a directory representing n, and lists 2..n-1 as its
 * children. For each integer m:
 *   - if m is prime, it is a regular file whose content is the decimal value
 *     of m;
 *   - if m is composite, it is a directory that recursively contains all
 *     smaller numbers 2..m-1.
 */

#include <cassert>
#include <charconv>
#include <condy.hpp>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <format>
#include <iostream>
#include <linux/fuse.h>
#include <memory>
#include <string>
#include <sys/mount.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <thread>
#include <unistd.h>

uint64_t number = 10;
size_t queue_depth = 8;
bool multi_thread = false;
std::string mountpoint;

constexpr auto *FUSE_DEV = "/dev/fuse";
constexpr auto *FUSE_NAME = "fuse-prime-fs";
constexpr size_t MAX_READAHEAD = 128ul * 1024;
constexpr size_t MAX_PAGES = 32;
constexpr size_t MAX_WRITE = 128ul * 1024;
const size_t MAX_PAYLOAD_SZ = std::max<size_t>(
    {FUSE_MIN_READ_BUFFER, MAX_WRITE, MAX_PAGES *sysconf(_SC_PAGESIZE)});
constexpr int FIXED_FD = 0;

void usage(const char *prog) {
    std::cerr << std::format("Usage: {} [OPTIONS] <mountpoint>\n"
                             "Options:\n"
                             "  -n NUM    Max node number (default: 10)\n"
                             "  -q NUM    Queue depth per CPU (default: 8)\n"
                             "  -m        Enable multi-thread mode\n"
                             "  -h        Show this help\n",
                             prog);
}

void mount_fuse(int fuse_fd, const std::string &mountpoint) {
    int fsfd = fsopen("fuse", 0);
    if (fsfd < 0) {
        std::perror("fsopen");
        std::exit(1);
    }

    if (fsconfig(fsfd, FSCONFIG_SET_STRING, "fd",
                 std::to_string(fuse_fd).c_str(), 0) < 0) {
        std::perror("fsconfig fd");
        std::exit(1);
    }
    if (fsconfig(fsfd, FSCONFIG_SET_STRING, "source", FUSE_NAME, 0) < 0) {
        std::perror("fsconfig source");
        std::exit(1);
    }
    if (fsconfig(fsfd, FSCONFIG_SET_STRING, "subtype", FUSE_NAME, 0) < 0) {
        std::perror("fsconfig subtype");
        std::exit(1);
    }
    std::string rootmode = std::format(
        "{:#o}", S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (fsconfig(fsfd, FSCONFIG_SET_STRING, "rootmode", rootmode.c_str(), 0) <
        0) {
        std::perror("fsconfig rootmode");
        std::exit(1);
    }
    if (fsconfig(fsfd, FSCONFIG_SET_STRING, "user_id",
                 std::to_string(getuid()).c_str(), 0) < 0) {
        std::perror("fsconfig user_id");
        std::exit(1);
    }
    if (fsconfig(fsfd, FSCONFIG_SET_STRING, "group_id",
                 std::to_string(getgid()).c_str(), 0) < 0) {
        std::perror("fsconfig group_id");
        std::exit(1);
    }
    if (fsconfig(fsfd, FSCONFIG_CMD_CREATE, nullptr, nullptr, 0) < 0) {
        std::perror("fsconfig create");
        std::exit(1);
    }

    int mfd = fsmount(fsfd, 0, 0);
    if (mfd < 0) {
        std::perror("fsmount");
        std::exit(1);
    }

    if (move_mount(mfd, "", AT_FDCWD, mountpoint.c_str(),
                   MOVE_MOUNT_F_EMPTY_PATH) < 0) {
        std::perror("move_mount");
        std::exit(1);
    }

    close(mfd);
    close(fsfd);
}

struct FuseInitReq {
    fuse_in_header in;
    fuse_init_in init;
};

struct FuseInitResp {
    fuse_out_header out;
    fuse_init_out init;
};

int check_init_request(const FuseInitReq *req, size_t n) {
    if (n < sizeof(FuseInitReq)) {
        std::cerr << "FUSE_INIT request too small\n";
        return -EPROTO;
    }
    if (req->in.opcode != FUSE_INIT) {
        std::cerr << std::format("Expected FUSE_INIT, got {}\n",
                                 req->in.opcode);
        return -EPROTO;
    }
    if (req->init.major != FUSE_KERNEL_VERSION) {
        std::cerr << std::format("Unsupported major version {}\n",
                                 req->init.major);
        return -EPROTO;
    }
    constexpr uint32_t FUSE_IO_URING_MINOR = 42;
    if (req->init.minor < FUSE_IO_URING_MINOR) {
        std::cerr << std::format("Unsupported minor version {}\n",
                                 req->init.minor);
        return -EOPNOTSUPP;
    }
    return 0;
}

void init_fuse(int fuse_fd) {
    static char buf[FUSE_MIN_READ_BUFFER];
    auto *req = reinterpret_cast<FuseInitReq *>(buf);
    auto *resp = reinterpret_cast<FuseInitResp *>(buf);

    ssize_t n = read(fuse_fd, buf, sizeof(buf));
    if (n < 0) {
        std::perror("read FUSE_INIT");
        std::exit(1);
    }
    if (int err = check_init_request(req, n); err != 0) {
        resp->out.len = sizeof(resp->out);
        resp->out.error = err;
        resp->out.unique = req->in.unique;
        if (write(fuse_fd, buf, resp->out.len) < 0) {
            std::perror("write FUSE_INIT error reply");
        }
        std::exit(1);
    }

    fuse_init_out init = {};
    init.major = FUSE_KERNEL_VERSION;
    init.minor = std::min<uint32_t>(FUSE_KERNEL_MINOR_VERSION, req->init.minor);
    init.max_write = MAX_WRITE;
    init.max_pages = MAX_PAGES;
    init.max_readahead = MAX_READAHEAD;
    init.flags =
        FUSE_ASYNC_READ | FUSE_BIG_WRITES | FUSE_MAX_PAGES | FUSE_INIT_EXT;
    init.flags2 = static_cast<uint32_t>(FUSE_OVER_IO_URING >> 32);

    resp->out.len = sizeof(*resp);
    resp->out.error = 0;
    resp->out.unique = req->in.unique;
    resp->init = init;

    n = write(fuse_fd, buf, resp->out.len);
    if (n < 0) {
        std::perror("write FUSE_INIT reply");
        std::exit(1);
    }
}

auto fuse_register_cmd(iovec iov[2], uint16_t qid) {
    return condy::async_uring_cmd(
        FUSE_IO_URING_CMD_REGISTER, condy::fixed(FIXED_FD),
        [iov, qid](io_uring_sqe *sqe) {
            sqe->addr = reinterpret_cast<uint64_t>(iov);
            sqe->len = 2;
            auto *cmd = reinterpret_cast<fuse_uring_cmd_req *>(sqe->cmd);
            *cmd = {};
            cmd->qid = qid;
        });
}

auto fuse_commit_and_fetch_cmd(uint16_t qid, uint64_t commit_id) {
    return condy::async_uring_cmd(
        FUSE_IO_URING_CMD_COMMIT_AND_FETCH, condy::fixed(FIXED_FD),
        [qid, commit_id](io_uring_sqe *sqe) {
            auto *cmd = reinterpret_cast<fuse_uring_cmd_req *>(sqe->cmd);
            *cmd = {};
            cmd->qid = qid;
            cmd->commit_id = commit_id;
        });
}

class FuseServer {
public:
    FuseServer(uint64_t number)
        : number_(number), now_(time(nullptr)), uid_(getuid()), gid_(getgid()) {
    }

    condy::Coro<void> handle(fuse_uring_req_header *hdr, void *payload) {
        auto opcode = reinterpret_cast<fuse_in_header *>(hdr->in_out)->opcode;
        Request req{hdr, payload};

        switch (opcode) {
        case FUSE_LOOKUP:
            do_lookup_(req);
            break;
        case FUSE_GETATTR:
            do_getattr_(req);
            break;
        case FUSE_OPEN:
        case FUSE_OPENDIR:
            do_open_(req);
            break;
        case FUSE_READ:
            do_read_(req);
            break;
        case FUSE_READDIR:
            do_readdir_(req);
            break;
        case FUSE_STATFS:
            do_statfs_(req);
            break;
        case FUSE_RELEASE:
        case FUSE_RELEASEDIR:
        case FUSE_FLUSH:
        case FUSE_FSYNC:
        case FUSE_FSYNCDIR:
        case FUSE_ACCESS:
        case FUSE_DESTROY:
            req.reply_err(0);
            break;
        default:
            req.reply_err(-ENOSYS);
            break;
        }
        co_return;
    }

private:
    struct Request {
        fuse_uring_req_header *hdr;
        void *payload;

        uint64_t nodeid() const { return in()->nodeid; }

        template <typename T> const T *op_in() const {
            return reinterpret_cast<const T *>(hdr->op_in);
        }

        void reply_err(int err) {
            out()->len = sizeof(*out());
            out()->error = err;
            out()->unique = in()->unique;
            hdr->ring_ent_in_out.payload_sz = 0;
        }

        void reply_ok(const void *data, size_t size) {
            out()->error = 0;
            assert(size <= MAX_PAYLOAD_SZ);
            if (data) {
                std::memcpy(payload, data, size);
            }
            out()->len = sizeof(*out()) + size;
            out()->unique = in()->unique;
            hdr->ring_ent_in_out.payload_sz = size;
        }

    private:
        fuse_in_header *in() const {
            return reinterpret_cast<fuse_in_header *>(hdr->in_out);
        }

        fuse_out_header *out() const {
            return reinterpret_cast<fuse_out_header *>(hdr->in_out);
        }
    };

private:
    void do_lookup_(Request &req) {
        uint64_t parent = req.nodeid();
        std::string_view name = static_cast<const char *>(req.payload);

        if (!is_dir_(parent)) {
            req.reply_err(-ENOTDIR);
            return;
        }

        uint64_t num = 0;
        auto [ptr, ec] = std::from_chars(name.begin(), name.end(), num);
        if (ec != std::errc() || ptr != name.end() || num <= 1 ||
            num >= num_of_(parent)) {
            req.reply_err(-ENOENT);
            return;
        }

        bool dir = is_dir_(num);
        uint64_t size =
            dir ? 4096 : static_cast<uint64_t>(std::to_string(num).size() + 1);

        fuse_entry_out out = {};
        out.nodeid = num;
        fill_attr_(out.attr, out.nodeid, dir, size);
        req.reply_ok(&out, sizeof(out));
    }

    void do_getattr_(Request &req) {
        uint64_t num = num_of_(req.nodeid());
        bool dir = is_dir_(req.nodeid());
        uint64_t size =
            dir ? 4096 : static_cast<uint64_t>(std::to_string(num).size() + 1);

        fuse_attr_out out = {};
        fill_attr_(out.attr, req.nodeid(), dir, size);
        req.reply_ok(&out, sizeof(out));
    }

    void do_open_(Request &req) {
        fuse_open_out out = {};
        req.reply_ok(&out, sizeof(out));
    }

    void do_read_(Request &req) {
        uint64_t num = num_of_(req.nodeid());
        if (!is_dir_(req.nodeid())) {
            std::string content = std::to_string(num) + "\n";
            auto *ri = req.op_in<fuse_read_in>();
            size_t off = std::min<size_t>(ri->offset, content.size());
            size_t n = std::min<size_t>(ri->size, content.size() - off);
            req.reply_ok(content.data() + off, n);
        } else {
            req.reply_err(-EISDIR);
        }
    }

    void do_readdir_(Request &req) {
        auto *ri = req.op_in<fuse_read_in>();
        size_t max_reply = std::min<size_t>(ri->size, MAX_PAYLOAD_SZ);
        size_t used = 0;

        uint64_t num = num_of_(req.nodeid());
        if (!is_dir_(req.nodeid())) {
            req.reply_err(-ENOTDIR);
            return;
        }

        auto emit = [payload = req.payload, max_reply,
                     &used](uint64_t ino, uint64_t entry_off, uint32_t type,
                            std::string_view name) -> bool {
            size_t namelen = name.size();
            size_t reclen = FUSE_REC_ALIGN(FUSE_NAME_OFFSET + namelen);
            auto *d = reinterpret_cast<fuse_dirent *>(
                static_cast<char *>(payload) + used);
            if (used + reclen > max_reply) {
                return false;
            }
            d->ino = ino;
            d->off = entry_off;
            d->namelen = namelen;
            d->type = type;
            std::memcpy(d->name, name.data(), namelen);
            used += reclen;
            return true;
        };

        // Range: [2, num)
        for (uint64_t i = ri->offset; i + 2 < num; i++) {
            uint64_t n = i + 2;
            std::string name = std::to_string(n);
            bool ok = emit(n, i + 1, is_dir_(n) ? DT_DIR : DT_REG, name);
            if (!ok) {
                break;
            }
        }

        req.reply_ok(nullptr, used);
    }

    void do_statfs_(Request &req) {
        fuse_statfs_out out = {};
        req.reply_ok(&out, sizeof(out));
    }

private:
    void fill_attr_(fuse_attr &attr, uint64_t ino, bool dir, uint64_t size) {
        constexpr uint32_t DIR_MODE =
            S_IFDIR | S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
        constexpr uint32_t FILE_MODE = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH;

        attr = {};
        attr.ino = ino;
        attr.size = size;
        attr.blocks = (size + 511) / 512;
        attr.atime = attr.mtime = attr.ctime = now_;
        attr.mode = dir ? DIR_MODE : FILE_MODE;
        attr.nlink = dir ? 2 : 1;
        attr.uid = uid_;
        attr.gid = gid_;
        attr.blksize = 4096;
    }

    static bool is_prime_(uint64_t n) {
        if (n < 2) {
            return false;
        }
        for (uint64_t i = 2; i * i <= n; i++) {
            if (n % i == 0) {
                return false;
            }
        }
        return true;
    }

    uint64_t num_of_(uint64_t nodeid) const {
        return nodeid == ROOT_NODEID ? number_ : nodeid;
    }

    bool is_dir_(uint64_t nodeid) const {
        return nodeid == ROOT_NODEID || !is_prime_(num_of_(nodeid));
    }

private:
    static constexpr uint64_t ROOT_NODEID = 1;

    uint64_t number_;
    time_t now_;
    uid_t uid_;
    gid_t gid_;
};

condy::Coro<void> io_loop(uint16_t qid, fuse_uring_req_header *hdr,
                          void *payload, FuseServer &server) {
    int r;
    iovec iov[] = {
        {hdr, sizeof(*hdr)},
        {payload, MAX_PAYLOAD_SZ},
    };

    r = co_await fuse_register_cmd(iov, qid);
    if (r == -ENOTCONN) {
        co_return;
    } else if (r < 0) {
        std::cerr << std::format("REGISTER failed: {}\n", strerror(-r));
        std::exit(1);
    }

    while (true) {
        uint64_t commit_id = hdr->ring_ent_in_out.commit_id;

        co_await server.handle(hdr, payload);

        r = co_await fuse_commit_and_fetch_cmd(qid, commit_id);
        if (r == -ENOTCONN) {
            co_return;
        } else if (r < 0) {
            std::cerr << std::format("COMMIT_AND_FETCH failed: {}\n",
                                     strerror(-r));
            std::exit(1);
        }
    }
}

condy::Coro<void> io_queue(int fuse_fd, uint16_t qid,
                           fuse_uring_req_header *queue_headers,
                           void *queue_payloads, FuseServer &server) {
    auto &fd_table = condy::current_runtime().fd_table();
    int r = fd_table.init(&fuse_fd, 1);
    if (r < 0) {
        std::cerr << std::format("fd_table.init failed: {}\n", r);
        std::exit(1);
    }

    std::vector<condy::Task<void>> tasks;
    tasks.reserve(queue_depth);
    for (size_t i = 0; i < queue_depth; i++) {
        tasks.push_back(condy::co_spawn(io_loop(
            qid, queue_headers + i,
            static_cast<char *>(queue_payloads) + i * MAX_PAYLOAD_SZ, server)));
    }
    for (auto &t : tasks) {
        co_await t;
    }

    fd_table.destroy();
}

void on_signal(int) { umount2(mountpoint.c_str(), MNT_DETACH); }

int main(int argc, char **argv) noexcept(false) {
    int opt;
    while ((opt = getopt(argc, argv, "n:q:mh")) != -1) {
        switch (opt) {
        case 'n':
            number = std::stoull(optarg);
            break;
        case 'q':
            queue_depth = std::stoull(optarg);
            break;
        case 'm':
            multi_thread = true;
            break;
        case 'h':
        default:
            usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    if (number < 2) {
        std::cerr << "fuse-uring: -n must be >= 2\n";
        usage(argv[0]);
        return 1;
    }
    if (queue_depth == 0) {
        std::cerr << "fuse-uring: -q must be >= 1\n";
        usage(argv[0]);
        return 1;
    }

    if (optind >= argc) {
        usage(argv[0]);
        return 1;
    }
    mountpoint = argv[optind];

    int fuse_fd = open(FUSE_DEV, O_RDWR | O_CLOEXEC);
    if (fuse_fd < 0) {
        std::perror("open /dev/fuse");
        return 1;
    }

    mount_fuse(fuse_fd, mountpoint);
    init_fuse(fuse_fd);

    size_t possible_cpus = get_nprocs_conf();

    size_t headers_size =
        possible_cpus * queue_depth * sizeof(fuse_uring_req_header);
    void *addr = mmap(nullptr, headers_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        std::perror("mmap");
        return 1;
    }
    auto *headers_base = reinterpret_cast<fuse_uring_req_header *>(addr);

    size_t payloads_size = possible_cpus * queue_depth * MAX_PAYLOAD_SZ;
    void *payloads_base = mmap(nullptr, payloads_size, PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (payloads_base == MAP_FAILED) {
        std::perror("mmap");
        return 1;
    }

    struct sigaction sa = {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    condy::RuntimeOptions options;
    options.enable_sqe128();

    std::vector<std::unique_ptr<condy::Runtime>> runtimes;
    if (multi_thread) {
        options.sq_size(queue_depth);
        for (size_t i = 0; i < possible_cpus; i++) {
            runtimes.push_back(std::make_unique<condy::Runtime>(options));
            options.enable_attach_wq(*runtimes[0]);
        }
    } else {
        options.sq_size(possible_cpus * queue_depth);
        runtimes.push_back(std::make_unique<condy::Runtime>(options));
    }

    FuseServer server(number);

    std::vector<condy::Task<void>> queue_tasks;
    queue_tasks.reserve(possible_cpus);
    for (size_t qid = 0; qid < possible_cpus; qid++) {
        auto *queue_headers = headers_base + qid * queue_depth;
        auto *queue_payloads = static_cast<char *>(payloads_base) +
                               qid * queue_depth * MAX_PAYLOAD_SZ;
        auto queue =
            io_queue(fuse_fd, qid, queue_headers, queue_payloads, server);
        auto &runtime = multi_thread ? *runtimes[qid] : *runtimes[0];
        queue_tasks.push_back(condy::co_spawn(runtime, std::move(queue)));
    }

    if (multi_thread) {
        std::vector<std::jthread> threads;
        threads.reserve(possible_cpus);
        for (size_t i = 0; i < possible_cpus; i++) {
            threads.emplace_back([&runtime = *runtimes[i]]() {
                runtime.allow_exit();
                runtime.run();
            });
        }
    } else {
        runtimes[0]->allow_exit();
        runtimes[0]->run();
    }

    for (auto &t : queue_tasks) {
        t.wait();
    }

    munmap(headers_base, headers_size);
    munmap(payloads_base, payloads_size);

    close(fuse_fd);

    return 0;
}