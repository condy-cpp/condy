/**
 * @file ring.hpp
 * @brief Wrapper classes for liburing interfaces.
 * @details This file defines wrapper classes around liburing, providing support
 * for most synchronous operations.
 */

#pragma once

#include "condy/condy_uring.hpp"
#include "condy/utils.hpp"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstring>

namespace condy {
namespace detail {

class Ring {
public:
    Ring(unsigned int entries, io_uring_params *params,
         [[maybe_unused]] void *buf, [[maybe_unused]] size_t buf_size,
         size_t submit_batch)
        : submit_batch_(submit_batch) {
        int r;
#if !IO_URING_CHECK_VERSION(2, 5) // >= 2.5
        if (params->flags & IORING_SETUP_NO_MMAP) {
            r = io_uring_queue_init_mem(entries, &ring_, params, buf, buf_size);
        } else {
            r = io_uring_queue_init_params(entries, &ring_, params);
        }
#else
        r = io_uring_queue_init_params(entries, &ring_, params);
#endif
        if (r < 0) {
            throw make_system_error("io_uring_queue_init_params", -r);
        }
    }

    ~Ring() { io_uring_queue_exit(&ring_); }

    CONDY_DELETE_COPY_MOVE(Ring);

public:
    void submit() noexcept {
        maybe_submit_count_ = 0;
        io_uring_submit(&ring_);
    }

    void maybe_submit() noexcept {
        maybe_submit_count_++;
        if (maybe_submit_count_ >= submit_batch_) {
            submit();
        }
    }

    template <typename Func>
    ssize_t reap_completions_wait(Func &&process_func) noexcept {
        unsigned head;
        io_uring_cqe *cqe;
        ssize_t reaped = 0;
        do {
            int r = io_uring_submit_and_wait(&ring_, 1);
            if (r >= 0) [[likely]] {
                maybe_submit_count_ = 0;
                break;
            } else if (r == -EINTR) {
                continue;
            } else {
                return r;
            }
        } while (true);

        io_uring_for_each_cqe(&ring_, head, cqe) {
            process_func(cqe);
#if !IO_URING_CHECK_VERSION(2, 13) // >= 2.13
            reaped += io_uring_cqe_nr(cqe);
#else
            reaped++;
#endif
        }
        io_uring_cq_advance(&ring_, reaped);
        return reaped;
    }

    template <typename Func>
    ssize_t reap_completions(Func &&process_func) noexcept {
        io_uring_cqe *cqe;
        int r = io_uring_peek_cqe(&ring_, &cqe);
        if (r == -EAGAIN) {
            return 0;
        } else if (r < 0) {
            return r;
        }

        unsigned head;
        ssize_t reaped = 0;
        io_uring_for_each_cqe(&ring_, head, cqe) {
            process_func(cqe);
#if !IO_URING_CHECK_VERSION(2, 13) // >= 2.13
            reaped += io_uring_cqe_nr(cqe);
#else
            reaped++;
#endif
        }
        io_uring_cq_advance(&ring_, reaped);
        return reaped;
    }

    void reserve_space(size_t n) noexcept {
        size_t space_left;
        do {
            space_left = io_uring_sq_space_left(&ring_);
            if (space_left >= n) {
                return;
            }
            submit();
        } while (true);
    }

    io_uring *ring() noexcept { return &ring_; }

    io_uring_sqe *get_sqe() noexcept { return get_sqe_<io_uring_get_sqe>(); }

#if !IO_URING_CHECK_VERSION(2, 13) // >= 2.13
    io_uring_sqe *get_sqe128() noexcept {
        if (ring_.flags & (IORING_SETUP_SQE128 | IORING_SETUP_SQE_MIXED))
            [[likely]] {
            return get_sqe_<io_uring_get_sqe128>();
        }
        return nullptr;
    }
#endif

private:
    template <io_uring_sqe *(*get_sqe)(struct io_uring *)>
    io_uring_sqe *get_sqe_() noexcept {
        [[maybe_unused]] int r;
        io_uring_sqe *sqe;
        do {
            sqe = get_sqe(&ring_);
            if (sqe) {
                break;
            }
            submit();
            if (ring_.flags & IORING_SETUP_SQPOLL) {
                r = io_uring_sqring_wait(&ring_);
                assert(r >= 0);
            }
        } while (true);
        return sqe;
    }

private:
    io_uring ring_;
    size_t submit_batch_;
    size_t maybe_submit_count_ = 0;
};

} // namespace detail
} // namespace condy