/**
 * @file task.hpp
 * @brief Interfaces for coroutine task management.
 * @details This file defines interfaces for running and managing concurrent
 * coroutine tasks in Condy.
 */

#pragma once

#include "condy/coro.hpp"
#include "condy/detail/context.hpp"
#include "condy/detail/task.hpp"
#include "condy/runtime.hpp"
#include <coroutine>
#include <exception>
#include <utility>

namespace condy {

/**
 * @brief Coroutine task that runs concurrently in the runtime.
 * @tparam T Return type of the coroutine.
 * @tparam Allocator Allocator type used for memory management, default is
 * `void`, which means use the default allocator.
 * @details Task is a handle to a coroutine task that can be awaited or
 * detached. The coroutine will be scheduled and executed in the associated
 * runtime. User can await the task to get the result of the coroutine, or
 * detach the task to let it run independently.
 * @warning Destroying a Task without awaiting or detaching it will cause a
 * panic.
 * @warning Unhandled exceptions in a detached task will also cause a panic.
 */
template <typename T = void, typename Allocator = void>
class [[nodiscard]] Task : public detail::TaskBase<T, Allocator> {
public:
    using Base = detail::TaskBase<T, Allocator>;
    using Base::Base;

    /**
     * @brief Wait synchronously for the task to complete and get the result.
     * @return T The result of the coroutine.
     * @throws std::invalid_argument If the task is not joinable.
     * @throws Any exception thrown inside the coroutine.
     * @details This function blocks the current thread until the coroutine
     * associated with the task completes. It then retrieves the result of the
     * coroutine. If the coroutine throws an exception, it will be rethrown
     * here.
     */
    T wait() {
        auto handle = std::exchange(Base::handle_, nullptr);
        Base::wait_inner_(handle);
        auto exception = std::move(handle.promise()).exception();
        if (exception) [[unlikely]] {
            handle.destroy();
            std::rethrow_exception(exception);
        }
        T value = std::move(handle.promise()).value();
        handle.destroy();
        return std::move(value);
    }
};

template <typename Allocator>
class [[nodiscard]]
Task<void, Allocator> : public detail::TaskBase<void, Allocator> {
public:
    using Base = detail::TaskBase<void, Allocator>;
    using Base::Base;

    /**
     * @brief Wait synchronously for the task to complete.
     * @throws std::invalid_argument If the task is not joinable.
     * @throws Any exception thrown inside the coroutine.
     * @details This function blocks the current thread until the coroutine
     * associated with the task completes. If the coroutine throws an exception,
     * it will be rethrown here.
     */
    void wait() {
        auto handle = std::exchange(Base::handle_, nullptr);
        Base::wait_inner_(handle);
        auto exception = std::move(handle.promise()).exception();
        handle.destroy();
        if (exception) [[unlikely]] {
            std::rethrow_exception(exception);
        }
    }
};

/**
 * @brief Spawn a coroutine as a task in the given runtime.
 * @tparam T Return type of the coroutine.
 * @tparam Allocator Allocator type used for memory management.
 * @param runtime The runtime to spawn the coroutine in.
 * @param coro The coroutine to be spawned.
 * @return Task<T, Allocator> The spawned task.
 * @details This function schedules the given coroutine to run as a task in the
 * specified runtime. The coroutine will be executed concurrently in the
 * runtime.
 */
template <typename T, typename Allocator>
inline Task<T, Allocator> co_spawn(Runtime &runtime,
                                   Coro<T, Allocator> coro) noexcept {
    auto handle = coro.release();
    auto &promise = handle.promise();
    promise.mark_running();

    runtime.schedule(&promise);
    return {handle};
}

/**
 * @brief Spawn a coroutine as a task in the current runtime.
 * @tparam T Return type of the coroutine.
 * @tparam Allocator Allocator type used for memory management.
 * @param coro The coroutine to be spawned.
 * @return Task<T, Allocator> The spawned task.
 * @throws std::logic_error If there is no current runtime.
 */
template <typename T, typename Allocator>
inline Task<T, Allocator> co_spawn(Coro<T, Allocator> coro) {
    auto *runtime = detail::Context::current().runtime();
    if (runtime == nullptr) [[unlikely]] {
        throw std::logic_error("No runtime to spawn coroutine task");
    }
    return co_spawn(*runtime, std::move(coro));
}

namespace detail {

struct [[nodiscard]] SwitchAwaiter {
    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    void await_suspend(std::coroutine_handle<PromiseType> handle) noexcept {
        runtime_->schedule(&handle.promise());
    }

    void await_resume() const noexcept {}

    Runtime *runtime_;
};

} // namespace detail

/**
 * @brief Switch current coroutine task to the given runtime.
 * @param runtime The runtime to switch to.
 * @return detail::SwitchAwaiter Awaiter object for the switch operation.
 * @details This function suspends the current coroutine and reschedules it to
 * run in the specified runtime. The caller coroutine will be resumed in the
 * target runtime.
 */
inline detail::SwitchAwaiter co_switch(Runtime &runtime) noexcept {
    return {&runtime};
}

} // namespace condy