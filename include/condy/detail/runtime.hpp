/**
 * @file runtime.hpp
 * @brief Runtime type for running the io_uring event loop.
 */

#pragma once

#include "condy/condy_uring.hpp"
#include "condy/detail/context.hpp"
#include "condy/detail/singleton.hpp"
#include "condy/ring.hpp"
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace condy {
namespace detail {

class ThreadLocalRing : public ThreadLocalSingleton<ThreadLocalRing> {
public:
    Ring *ring() { return &ring_; }

    ThreadLocalRing() : ring_(create_ring_()) {}

private:
    // NOLINTNEXTLINE(bugprone-exception-escape)
    static Ring create_ring_() noexcept {
        io_uring_params params = {};
        params.flags |= IORING_SETUP_CLAMP;
        params.flags |= IORING_SETUP_SINGLE_ISSUER;
        params.flags |= IORING_SETUP_SUBMIT_ALL;
        // If we can construct Runtime, we should be able to construct this
        // thread-local ring. So we ignore errors here.
        return Ring(8, &params, nullptr, 0, std::numeric_limits<size_t>::max());
    }

private:
    Ring ring_;
};

inline int sync_msg_ring(io_uring_sqe *sqe_data) noexcept {
#if !IO_URING_CHECK_VERSION(2, 12) // >= 2.12
    return io_uring_register_sync_msg(sqe_data);
#else
    auto *ring = ThreadLocalRing::current().ring();
    auto *sqe = ring->get_sqe();
    *sqe = *sqe_data;
    int r = 0;
    auto n =
        ring->reap_completions_wait([&](io_uring_cqe *cqe) { r = cqe->res; });
    if (n < 0) {
        return static_cast<int>(n);
    }
    assert(n == 1);
    return r;
#endif
}

class CancelRequest {
public:
    CancelRequest(uintptr_t data) : data_(data) {}

    void wait() noexcept {
        while (!finished_.load(std::memory_order_acquire)) {
            finished_.wait(false, std::memory_order_relaxed);
        }
    }

    void notify() noexcept {
        finished_.store(true, std::memory_order_release);
        finished_.notify_one();
    }

    uintptr_t data() const noexcept { return data_; }

private:
    uintptr_t data_;
    std::atomic_bool finished_ = false;
};

class OpFinishHandleBase {
public:
    using HandleFunc = bool (*)(void *, io_uring_cqe *) noexcept;

    bool handle(io_uring_cqe *cqe) noexcept {
        assert(handle_func_ != nullptr);
        return handle_func_(this, cqe);
    }

protected:
    OpFinishHandleBase() = default;

protected:
    HandleFunc handle_func_ = nullptr;
};

} // namespace detail
} // namespace condy
