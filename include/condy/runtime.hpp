/**
 * @file runtime.hpp
 * @brief Runtime type for running the io_uring event loop.
 */

#pragma once

#include "condy/condy_uring.hpp"
#include "condy/detail/context.hpp"
#include "condy/detail/intrusive.hpp"
#include "condy/detail/invoker.hpp"
#include "condy/detail/ring.hpp"
#include "condy/detail/runtime.hpp"
#include "condy/detail/utils.hpp"
#include "condy/detail/work_type.hpp"
#include "condy/ring_settings.hpp"
#include "condy/runtime_options.hpp"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

namespace condy {

/**
 * @brief The event loop runtime for executing asynchronous
 * @details This class provides a single-threaded runtime for executing
 * asynchronous tasks using io_uring. It manages the event loop, scheduling,
 * and execution of tasks, as well as inter-runtime notifications.
 */
class Runtime {
public:
    /**
     * @brief Construct a new Runtime object
     * @param options Options for configuring the runtime.
     */
    Runtime(const RuntimeOptions &options = {})
        : ring_(create_ring_(options)),
          event_interval_(options.event_interval_),
          disable_register_ring_fd_(options.disable_register_ring_fd_),
          fd_table_(*ring_.ring()), buffer_table_(*ring_.ring()),
          settings_(*ring_.ring()) {}

    CONDY_DELETE_COPY_MOVE(Runtime);

public:
    void schedule_internal(detail::WorkInvoker *work) noexcept {
        auto *curr_runtime = detail::Context::current().runtime();
        if (curr_runtime == this) {
            local_queue_.push_back(work);
            return;
        }

        if (ring_enabled_.load(std::memory_order_relaxed)) {
            // Fast path: if the ring is enabled, we can directly schedule the
            // work
            detail::tsan_release(work);
            schedule_msg_ring_(
                curr_runtime,
                detail::encode_work(work, detail::WorkType::Schedule));
        } else {
            // Slow path: if the ring is not enabled, we need to acquire the
            // mutex to ensure the work is scheduled before the ring is enabled
            std::unique_lock<std::mutex> lock(mutex_);
            if (ring_enabled_.load(std::memory_order_relaxed)) {
                lock.unlock();
                detail::tsan_release(work);
                schedule_msg_ring_(
                    curr_runtime,
                    detail::encode_work(work, detail::WorkType::Schedule));
            } else {
                global_queue_.push_back(work);
            }
        }
    }

    void cancel_internal(uintptr_t data) noexcept {
        auto *curr_runtime = detail::Context::current().runtime();
        if (curr_runtime == this) {
            io_uring_sqe *sqe = ring_.get_sqe();
            prep_cancel_(sqe, data);
            return;
        }

        if (!ring_enabled_.load(std::memory_order_relaxed)) {
            return;
        }

        // Potential address reuse problem?
        schedule_msg_ring_(curr_runtime, detail::encode_work_ptr(
                                             data, detail::WorkType::Cancel));
        if (curr_runtime != nullptr) {
            // Ensure the cancel msg is submitted.
            curr_runtime->ring_.submit();
        }
    }

    void pend_work_internal() noexcept {
        assert(detail::Context::current().runtime() == this);
        pending_works_++;
    }

    void resume_work_internal() noexcept {
        assert(detail::Context::current().runtime() == this);
        pending_works_--;
    }

    auto &bgid_pool_internal() noexcept { return bgid_pool_; }

    auto &ring_internal() noexcept { return ring_; }

public:
    /**
     * @brief Allow the runtime to exit when there are no pending works.
     * @details By default, the runtime will keep running even if there are no
     * pending works. Calling this function will allow the runtime to exit
     * once all pending works are completed.
     * @note This function is thread-safe and can be called from any thread.
     */
    void allow_exit() noexcept {
        exit_allowed_.store(true, std::memory_order_release);
        wakeup_();
    }

    /**
     * @brief Run the runtime event loop in the current thread.
     * @details Runs the event loop in the current thread until there are no
     * pending works and exit is allowed. Can be called multiple times from
     * the same thread.
     * @note This function can only be called from one thread. Calling it from
     * multiple threads, either concurrently or sequentially is not allowed.
     */
    void run() {
        detail::Context::current().init(this);
        auto d = detail::defer([] { detail::Context::current().reset(); });

        if (run_thread_id_ == std::thread::id()) {
            int r = io_uring_enable_rings(ring_.ring());
            if (r < 0) {
                throw detail::make_system_error("io_uring_enable_rings", -r);
            }

            run_thread_id_ = std::this_thread::get_id();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                flush_global_queue_();
                ring_enabled_.store(true, std::memory_order_relaxed);
            }
        } else if (run_thread_id_ == std::this_thread::get_id()) {
            flush_ring_();
        } else {
            throw std::runtime_error(
                "Runtime::run() can only be called from the same thread");
        }

        if (!disable_register_ring_fd_) {
            io_uring_register_ring_fd(ring_.ring());
        }
        auto d2 = detail::defer([&] {
            if (!disable_register_ring_fd_) {
                io_uring_unregister_ring_fd(ring_.ring());
            }
        });

        auto d3 = detail::defer([&] {
            tick_count_ = 0;
            exit_allowed_.store(false, std::memory_order_release);
        });
        while (true) {
            tick_count_++;

            if (tick_count_ >= event_interval_) {
                tick_count_ = 0;
                flush_ring_();
            }

            if (auto *work = local_queue_.pop_front()) {
                (*work)();
                continue;
            }

            if (pending_works_ == 0 &&
                exit_allowed_.load(std::memory_order_acquire)) {
                break;
            }
            flush_ring_wait_();
        }
    }

    /**
     * @brief Get the file descriptor table of the runtime.
     * @return FdTable& Reference to the fd table of the runtime.
     */
    auto &fd_table() noexcept { return fd_table_; }

    /**
     * @brief Get the buffer table of the runtime.
     * @return BufferTable& Reference to the buffer table of the runtime.
     */
    auto &buffer_table() noexcept { return buffer_table_; }

    /**
     * @brief Get the ring settings of the runtime.
     * @return RingSettings& Reference to the ring settings of the runtime.
     */
    auto &settings() noexcept { return settings_; }

private:
    static detail::Ring create_ring_(const RuntimeOptions &options) {
        io_uring_params params;
        std::memset(&params, 0, sizeof(params));

        params.flags |= IORING_SETUP_CLAMP;
        params.flags |= IORING_SETUP_SINGLE_ISSUER;
        params.flags |= IORING_SETUP_SUBMIT_ALL;
        params.flags |= IORING_SETUP_R_DISABLED;

        size_t ring_entries = options.sq_size_;
        if (options.cq_size_ != 0) { // 0 means default
            params.flags |= IORING_SETUP_CQSIZE;
            params.cq_entries = options.cq_size_;
        }

        if (options.enable_iopoll_) {
            params.flags |= IORING_SETUP_IOPOLL;
#if CONDY_URING_VERSION_GE(2, 9) // >= 2.9
            if (options.enable_hybrid_iopoll_) {
                params.flags |= IORING_SETUP_HYBRID_IOPOLL;
            }
#endif
        }

        if (options.enable_sqpoll_) {
            params.flags |= IORING_SETUP_SQPOLL;
            params.sq_thread_idle = options.sqpoll_idle_time_ms_;
            if (options.sqpoll_thread_cpu_.has_value()) {
                params.flags |= IORING_SETUP_SQ_AFF;
                params.sq_thread_cpu = *options.sqpoll_thread_cpu_;
            }
        }

        if (options.attach_wq_target_ != nullptr) {
            params.flags |= IORING_SETUP_ATTACH_WQ;
            params.wq_fd = options.attach_wq_target_->ring_.ring()->ring_fd;
        }

        if (options.enable_defer_taskrun_) {
            params.flags |= IORING_SETUP_DEFER_TASKRUN;
            params.flags |= IORING_SETUP_TASKRUN_FLAG;
        }

        if (options.enable_coop_taskrun_) {
            params.flags |= IORING_SETUP_COOP_TASKRUN;
            params.flags |= IORING_SETUP_TASKRUN_FLAG;
        }

        if (options.enable_sqe128_) {
            params.flags |= IORING_SETUP_SQE128;
        }

        if (options.enable_cqe32_) {
            params.flags |= IORING_SETUP_CQE32;
        }

#if CONDY_URING_VERSION_GE(2, 13) // >= 2.13
        if (options.enable_sqe_mixed_) {
            params.flags |= IORING_SETUP_SQE_MIXED;
        }
#endif

#if CONDY_URING_VERSION_GE(2, 13) // >= 2.13
        if (options.enable_cqe_mixed_) {
            params.flags |= IORING_SETUP_CQE_MIXED;
        }
#endif

        void *buf = nullptr;
        size_t buf_size = 0;
#if CONDY_URING_VERSION_GE(2, 5) // >= 2.5
        if (options.enable_no_mmap_) {
            params.flags |= IORING_SETUP_NO_MMAP;
            buf = options.no_mmap_buf_;
            buf_size = options.no_mmap_buf_size_;
        }
#endif

#if CONDY_URING_VERSION_GE(2, 14) // >= 2.14
        if (options.enable_sq_rewind_) {
            params.flags |= IORING_SETUP_SQ_REWIND;
        }
#endif

        size_t submit_batch = options.submit_batch_;
        if (submit_batch == 0) {
            if (options.enable_sqpoll_) {
                submit_batch = std::min<size_t>(32, ring_entries);
            } else {
                submit_batch = std::numeric_limits<size_t>::max();
            }
        }
        assert(submit_batch > 0);

        return detail::Ring(ring_entries, &params, buf, buf_size, submit_batch);
    }

    void schedule_msg_ring_(Runtime *curr_runtime, uintptr_t data) noexcept {
        int ring_fd = this->ring_.ring()->ring_fd;
        if (curr_runtime != nullptr) {
            io_uring_sqe *sqe = curr_runtime->ring_.get_sqe();
            prep_msg_ring_(ring_fd, sqe, data);
            curr_runtime->pend_work_internal();
        } else {
            io_uring_sqe sqe = {};
            prep_msg_ring_(ring_fd, &sqe, data);
            int r = detail::sync_msg_ring(&sqe);
            if (r < 0) {
                detail::panic_on(
                    std::format("sync_msg_ring: {}", std::strerror(-r)));
            }
        }
    }

    // Wakeup the runtime if it's blocked in Ring::reap_completions_wait()
    void wakeup_() noexcept {
        auto *curr_runtime = detail::Context::current().runtime();
        if (curr_runtime == this) {
            return;
        }

        if (!ring_enabled_.load(std::memory_order_relaxed)) {
            return;
        }

        schedule_msg_ring_(
            curr_runtime,
            detail::encode_work(nullptr, detail::WorkType::Ignore));
        if (curr_runtime != nullptr) {
            // Ensure the wakeup msg is submitted. Since user may call
            // thread.join() after allow_exit()
            curr_runtime->ring_.submit();
        }
    }

    void flush_global_queue_() noexcept {
        local_queue_.push_back(std::move(global_queue_));
    }

    static void prep_msg_ring_(int ring_fd, io_uring_sqe *sqe,
                               uintptr_t data) noexcept {
        io_uring_prep_msg_ring(sqe, ring_fd, 0, data, 0);
        io_uring_sqe_set_data64(
            sqe, detail::encode_work(nullptr, detail::WorkType::Schedule));
    }

    static void prep_cancel_(io_uring_sqe *sqe, uintptr_t data) noexcept {
        io_uring_prep_cancel64(sqe, data, 0);
        io_uring_sqe_set_data64(
            sqe, detail::encode_work(nullptr, detail::WorkType::Ignore));
        io_uring_sqe_set_flags(sqe, IOSQE_CQE_SKIP_SUCCESS);
    }

    void flush_ring_() noexcept {
        auto r = ring_.reap_completions(
            [this](io_uring_cqe *cqe) { process_cqe_(cqe); });
        if (r < 0) {
            detail::panic_on(std::format("io_uring_peek_cqe: {}",
                                         std::strerror(static_cast<int>(-r))));
        }
    }

    void flush_ring_wait_() noexcept {
        auto r = ring_.reap_completions_wait(
            [this](io_uring_cqe *cqe) { process_cqe_(cqe); });
        if (r < 0) {
            detail::panic_on(std::format("io_uring_submit_and_wait: {}",
                                         std::strerror(static_cast<int>(-r))));
        }
    }

    void process_cqe_(io_uring_cqe *cqe) noexcept {
        auto [data, type] = detail::decode_work(io_uring_cqe_get_data64(cqe));

        if (type == detail::WorkType::Ignore) {
            // No-op
            assert(cqe->res != -EINVAL); // If EINVAL, something is wrong
        } else if (type == detail::WorkType::Schedule) {
            if (data == nullptr) {
                if (cqe->res < 0) {
                    detail::panic_on(std::format("io_uring_prep_msg_ring: {}",
                                                 std::strerror(-cqe->res)));
                }
                resume_work_internal();
            } else {
                auto *work = static_cast<detail::WorkInvoker *>(data);
                detail::tsan_acquire(work);
                (*work)();
            }
        } else if (type == detail::WorkType::Cancel) {
            io_uring_sqe *sqe = ring_.get_sqe();
            prep_cancel_(sqe, reinterpret_cast<uintptr_t>(data));
        } else if (type == detail::WorkType::Common) {
            auto *handle = static_cast<detail::OpFinishHandleBase *>(data);
            auto op_finish = handle->handle(cqe);
            if (op_finish) {
                resume_work_internal();
            }
        } else {
            detail::unreachable();
        }
    }

private:
    using WorkListQueue =
        detail::IntrusiveSingleList<detail::WorkInvoker,
                                    &detail::WorkInvoker::work_queue_entry_>;

    // Global state
    std::mutex mutex_;
    WorkListQueue global_queue_;
    std::atomic_bool exit_allowed_ = false;
    std::atomic_bool ring_enabled_ = false;

    // Local state
    WorkListQueue local_queue_;
    detail::Ring ring_;
    size_t pending_works_ = 0;
    size_t tick_count_ = 0;
    std::thread::id run_thread_id_;

    // Configurable parameters
    size_t event_interval_ = 61;
    bool disable_register_ring_fd_ = false;

    FdTable fd_table_;
    BufferTable buffer_table_;
    RingSettings settings_;
    detail::IdPool<uint16_t> bgid_pool_;
};

/**
 * @brief Get the current runtime.
 * @return Runtime& Reference to the current running runtime.
 * @note This function assumes that there is a current runtime. Calling this
 * function outside of a coroutine will lead to undefined behavior.
 */
inline auto &current_runtime() noexcept {
    return *detail::Context::current().runtime();
}

} // namespace condy