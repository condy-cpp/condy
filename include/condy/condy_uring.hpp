/**
 * @file condy_uring.hpp
 */

#pragma once

#include <liburing.h>

#include <cerrno>
#include <cstring>
#include <sys/mman.h>

// liburing <= 2.3 has no version macros, define them here

#ifndef IO_URING_VERSION_MAJOR
#define IO_URING_VERSION_MAJOR 2
#endif

#ifndef IO_URING_VERSION_MINOR
#define IO_URING_VERSION_MINOR 3
#endif

#ifndef IO_URING_CHECK_VERSION
#define IO_URING_CHECK_VERSION(major, minor)                                   \
    (major > IO_URING_VERSION_MAJOR ||                                         \
     (major == IO_URING_VERSION_MAJOR && minor > IO_URING_VERSION_MINOR))
#endif

// Polyfill for io_uring_prep_uring_cmd (added in liburing 2.13)
// Opcode exists since 2.3, only the helper function is missing
#if IO_URING_CHECK_VERSION(2, 13) // < 2.13
inline void io_uring_prep_uring_cmd(struct io_uring_sqe *sqe, int cmd_op,
                                    int fd) noexcept {
    sqe->opcode = (__u8)IORING_OP_URING_CMD;
    sqe->fd = fd;
    sqe->cmd_op = cmd_op;
    sqe->__pad1 = 0;
    sqe->addr = 0ul;
    sqe->len = 0;
}
#endif

// Polyfill for io_uring_setup_buf_ring / io_uring_free_buf_ring
// (added in liburing 2.4)
#if IO_URING_CHECK_VERSION(2, 4) // < 2.4
inline struct io_uring_buf_ring *
io_uring_setup_buf_ring(struct io_uring *ring, unsigned int nentries, int bgid,
                        unsigned int flags, int *err) noexcept {
    size_t ring_size = nentries * sizeof(struct io_uring_buf);
    auto *br = static_cast<struct io_uring_buf_ring *>(
        mmap(nullptr, ring_size, PROT_READ | PROT_WRITE,
             MAP_ANONYMOUS | MAP_PRIVATE, -1, 0));
    if (br == MAP_FAILED) {
        *err = -errno;
        return nullptr;
    }

    io_uring_buf_ring_init(br);

    struct io_uring_buf_reg reg = {};
    reg.ring_addr = reinterpret_cast<uint64_t>(br);
    reg.ring_entries = nentries;
    reg.bgid = bgid;

    *err = 0;
    int ret = io_uring_register_buf_ring(ring, &reg, flags);
    if (ret != 0) {
        munmap(br, ring_size);
        *err = ret;
        return nullptr;
    }

    return br;
}

inline int io_uring_free_buf_ring(struct io_uring *ring,
                                  struct io_uring_buf_ring *br,
                                  unsigned int nentries, int bgid) noexcept {
    int ret = io_uring_unregister_buf_ring(ring, bgid);
    if (ret != 0) {
        return ret;
    }
    munmap(br, nentries * sizeof(struct io_uring_buf));
    return 0;
}
#endif
