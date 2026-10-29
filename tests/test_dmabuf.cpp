#include "condy.hpp"

#if CONDY_URING_VERSION_GE(2, 16) // >= 2.16
#include <cstdint>
#include <cstdlib>
#include <doctest.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>

namespace {

class DMABufResources {
public:
    DMABufResources() {
        bool ok = false;
        auto cleanup = condy::detail::defer([&] {
            if (!ok) {
                release_();
            }
        });
        heap_ = open("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
        REQUIRE(heap_ >= 0);
        dma_heap_allocation_data alloc{};
        alloc.len = size_;
        alloc.fd_flags = O_RDWR | O_CLOEXEC;
        REQUIRE(ioctl(heap_, DMA_HEAP_IOCTL_ALLOC, &alloc) == 0);
        dma_ = alloc.fd;
        mapping_ =
            mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, dma_, 0);
        REQUIRE(mapping_ != MAP_FAILED);
        ok = true;
    }

    CONDY_DELETE_COPY_MOVE(DMABufResources);

    ~DMABufResources() { release_(); }

    int dma_fd() const noexcept { return dma_; }
    void *data() const noexcept { return mapping_; }
    size_t size() const noexcept { return size_; }

    void sync(uint64_t flags) const {
        dma_buf_sync sync{.flags = flags};
        REQUIRE(ioctl(dma_, DMA_BUF_IOCTL_SYNC, &sync) == 0);
    }

private:
    void release_() noexcept {
        if (mapping_ != MAP_FAILED) {
            munmap(mapping_, size_);
        }
        for (int fd : {heap_, dma_}) {
            if (fd >= 0) {
                close(fd);
            }
        }
    }

    int heap_ = -1;
    int dma_ = -1;
    void *mapping_ = MAP_FAILED;
    size_t size_ = 8192;
};

} // namespace

TEST_CASE("test dmabuf - invalid registration preserves ordinary buffers") {
    auto task = []() -> condy::Coro<void> {
        auto &table = condy::current_runtime().buffer_table();
        REQUIRE(table.init(2) == 0);
        // Old kernels reject extended updates; new kernels reject these fds.
        io_uring_regbuf_desc desc{};
        desc.type = IO_REGBUF_TYPE_DMABUF;
        desc.dmabuf_fd = -1;
        desc.target_fd = -1;
        REQUIRE(table.update(0, &desc, 1) < 0);
        char data[32] = {};
        iovec vec{.iov_base = data, .iov_len = sizeof(data)};
        REQUIRE(table.update(0, &vec, 1) == 1);
        REQUIRE(table.destroy() == 0);
        co_return;
    };
    condy::sync_wait(task());
}

TEST_CASE("test buffer_table - extended descriptors") {
    auto task = []() -> condy::Coro<void> {
        auto &table = condy::current_runtime().buffer_table();
        REQUIRE(table.init(2) == 0);
        char bytes[32]{};
        io_uring_regbuf_desc descs[2]{};
        descs[0].type = IO_REGBUF_TYPE_UADDR;
        descs[0].size = sizeof(bytes);
        descs[0].uaddr = reinterpret_cast<uint64_t>(bytes);
        int r = table.update(0, descs, 2);
        if (r == -EINVAL) {
            MESSAGE("Kernel does not support extended buffer updates");
            co_return;
        }
        REQUIRE(r == 2);
        descs[1].type = ~0U;
        REQUIRE(table.update(0, descs, 2) == 1);
        REQUIRE(table.update(0, descs + 1, 1) == -EINVAL);
        io_uring_regbuf_desc empty{};
        REQUIRE(table.update(0, &empty, 1) == 1);
        REQUIRE(table.destroy() == 0);
        co_return;
    };
    condy::sync_wait(task());
}

// This test overwrites the first 4KiB of an explicitly supplied scratch NVMe
// device. Run in QEMU with CONDY_TEST_NVME_DEVICE_PATH=/dev/nvme0n1.
TEST_CASE("test dmabuf - NVMe read write and clone") {
    const char *device = std::getenv("CONDY_TEST_NVME_DEVICE_PATH");
    if (device == nullptr || device[0] == '\0') {
        MESSAGE("CONDY_TEST_NVME_DEVICE_PATH not set, skipping");
        return;
    }

    auto task = [&]() -> condy::Coro<void> {
        auto &runtime = condy::current_runtime();
        auto &table = runtime.buffer_table();
        REQUIRE(table.init(2) == 0);
        io_uring_regbuf_desc desc{};
        desc.type = IO_REGBUF_TYPE_DMABUF;
        desc.dmabuf_fd = -1;
        desc.target_fd = -1;
        // A supported dma-buf registration reaches fd lookup and returns
        // EBADF. Probe before allocating resources or opening the device.
        int r = table.update(0, &desc, 1);
        if (r == -EINVAL || r == -EOPNOTSUPP || r == -ENOSYS) {
            MESSAGE("Kernel does not support dma-buf registration, skipping");
            co_return;
        }
        REQUIRE(r == -EBADF);

        DMABufResources resources;
        int target = open(device, O_RDWR | O_DIRECT | O_CLOEXEC);
        REQUIRE(target >= 0);
        auto close_target = condy::detail::defer([&] { close(target); });
        int other = open(device, O_RDWR | O_DIRECT | O_CLOEXEC);
        REQUIRE(other >= 0);
        auto close_other = condy::detail::defer([&] { close(other); });
        desc.dmabuf_fd = resources.dma_fd();
        desc.target_fd = target;
        r = table.update(0, &desc, 1);
        if (r == -EOPNOTSUPP || r == -ENODEV) {
            MESSAGE("Device does not support dma-buf I/O, skipping");
            co_return;
        }
        REQUIRE(r == 1);
        REQUIRE(table.update(2, &desc, 1) == -EINVAL);

        condy::Runtime poll_runtime(condy::RuntimeOptions().enable_iopoll());
        auto &poll_table = poll_runtime.buffer_table();
        REQUIRE(poll_table.init(2) == 0);
        REQUIRE(poll_table.update(0, &desc, 1) == -EOPNOTSUPP);
        REQUIRE(poll_table.clone_buffers(table, 0, 0, 1) == -EOPNOTSUPP);
        REQUIRE(poll_table.destroy() == 0);

        auto *bytes = static_cast<unsigned char *>(resources.data());
        resources.sync(DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);
        for (size_t i = 0; i < resources.size(); ++i) {
            bytes[i] = static_cast<unsigned char>(i * 37 + i / 256);
        }
        resources.sync(DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);

        // A nonzero buffer offset is encoded in sqe->addr, not a CPU address.
        int index = 0;
        auto range = condy::fixed(
            index,
            condy::buffer(reinterpret_cast<void *>(uintptr_t{4096}), 4096));
        REQUIRE(co_await condy::async_write(target, range, 0) == 4096);
        REQUIRE(co_await condy::async_read(other, range, 0) == -EBADF);
        REQUIRE(co_await condy::async_read(
                    target,
                    condy::fixed(index, condy::buffer(reinterpret_cast<void *>(
                                                          resources.size()),
                                                      4096)),
                    0) == -EFAULT);

        // Clone to an independent table, release the original entry, and
        // clone back at a new index. This also checks shared kernel lifetime.
        condy::Runtime clone_runtime;
        auto &cloned = clone_runtime.buffer_table();
        REQUIRE(cloned.clone_buffers(table, 0, 0, 1) == 0);
        iovec empty{};
        REQUIRE(table.update(0, &empty, 1) == 1);
        REQUIRE(table.clone_buffers(cloned, 1, 0, 1) == 0);
        REQUIRE(cloned.destroy() == 0);
        index = 1;

        int target_fds[] = {target};
        REQUIRE(runtime.fd_table().init(target_fds, 1) == 0);
        range = condy::fixed(index,
                             condy::buffer(static_cast<void *>(nullptr), 4096));
        REQUIRE(co_await condy::async_read(condy::fixed(0), range, 0) == 4096);
        resources.sync(DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
        CHECK(std::memcmp(bytes, bytes + 4096, 4096) == 0);
        resources.sync(DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);

        // Replacing a dma-buf with ordinary memory must release it normally.
        char data[32]{};
        iovec vec{.iov_base = data, .iov_len = sizeof(data)};
        REQUIRE(table.update(index, &vec, 1) == 1);
        REQUIRE(table.destroy() == 0);
        REQUIRE(runtime.fd_table().destroy() == 0);
        co_return;
    };
    condy::sync_wait(task());
}

#endif
