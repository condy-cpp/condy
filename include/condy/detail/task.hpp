/**
 * @file task.hpp
 * @brief Interfaces for coroutine task management.
 * @details This file defines interfaces for running and managing concurrent
 * coroutine tasks in Condy.
 */

#pragma once

#include "condy/coro.hpp"
#include "condy/detail/context.hpp"
#include "condy/detail/invoker.hpp"
#include "condy/runtime.hpp"
#include <coroutine>
#include <future>

namespace condy {
namespace detail {

template <typename T = void, typename Allocator = void> class TaskBase {
public:
    using PromiseType = typename Coro<T, Allocator>::promise_type;

    TaskBase() : TaskBase(nullptr) {}
    TaskBase(std::coroutine_handle<PromiseType> h) : handle_(h) {}
    TaskBase(TaskBase &&other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)) {}
    TaskBase &operator=(TaskBase &&other) noexcept {
        if (this != &other) {
            if (handle_) {
                panic_on("Task destroyed without being awaited");
            }
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    CONDY_DELETE_COPY(TaskBase);

    ~TaskBase() {
        if (handle_) {
            panic_on("Task destroyed without being awaited");
        }
    }

public:
    /**
     * @brief Detach the task to run independently.
     * @details This function detaches the task, allowing it to run
     * independently in the associated runtime. The caller will not be able to
     * await the task or retrieve its result after detachment.
     * @warning Unhandled exceptions in a detached task will cause a panic.
     */
    void detach() noexcept {
        handle_.promise().request_detach();
        handle_ = nullptr;
    }

    /**
     * @brief Check if the task is still joinable. Similar to
     * `std::thread::joinable()`.
     */
    bool joinable() const noexcept { return handle_ != nullptr; }

    /**
     * @brief Await the task asynchronously.
     * @return T The result of the coroutine.
     * @throw std::invalid_argument If the task is not joinable.
     * @throws Any exception thrown inside the coroutine.
     * @details This function allows the caller to await the completion of the
     * coroutine associated with the task. It suspends the caller coroutine
     * until the task completes, and then retrieves the result of the coroutine.
     * If the coroutine throws an exception, it will be rethrown here.
     */
    auto operator co_await() noexcept;

protected:
    static void wait_inner_(std::coroutine_handle<PromiseType> handle);

protected:
    std::coroutine_handle<PromiseType> handle_;
};

template <typename T, typename Allocator>
void TaskBase<T, Allocator>::wait_inner_(
    std::coroutine_handle<PromiseType> handle) {
    if (Context::current().runtime() != nullptr) [[unlikely]] {
        throw std::logic_error("Sync wait inside runtime");
    }
    if (handle == nullptr) [[unlikely]] {
        throw std::invalid_argument("Task not joinable");
    }
    std::promise<void> prom;
    auto fut = prom.get_future();
    struct TaskWaiter : public InvokerAdapter<TaskWaiter> {
        TaskWaiter(std::promise<void> &p) : prom_(p) {}

        void invoke() noexcept { prom_.set_value(); }

        std::promise<void> &prom_;
    };

    TaskWaiter waiter(prom);
    if (handle.promise().request_join(&waiter)) {
        // Still not finished, wait
        fut.get();
    }
}

template <typename T, typename Allocator>
struct TaskAwaiterBase : public InvokerAdapter<TaskAwaiterBase<T, Allocator>> {
    TaskAwaiterBase(
        std::coroutine_handle<typename Coro<T, Allocator>::promise_type>
            task_handle,
        Runtime *runtime)
        : task_handle_(task_handle), runtime_(runtime) {}

    bool await_ready() const {
        if (task_handle_ == nullptr) {
            throw std::invalid_argument("Task not joinable");
        }
        return false;
    }

    template <typename PromiseType>
    bool
    await_suspend(std::coroutine_handle<PromiseType> caller_handle) noexcept {
        Context::current().runtime()->pend_work_internal();
        assert(runtime_ != nullptr);
        caller_promise_ = &caller_handle.promise();
        return task_handle_.promise().request_join(this);
    }

    void invoke() noexcept {
        assert(caller_promise_ != nullptr);
        runtime_->schedule_internal(caller_promise_);
    }

    std::coroutine_handle<typename Coro<T, Allocator>::promise_type>
        task_handle_;
    Runtime *runtime_ = nullptr;
    WorkInvoker *caller_promise_ = nullptr;
};

template <typename T, typename Allocator>
struct TaskAwaiter : public TaskAwaiterBase<T, Allocator> {
    using Base = TaskAwaiterBase<T, Allocator>;
    using Base::Base;

    T await_resume() {
        Context::current().runtime()->resume_work_internal();
        auto exception = std::move(Base::task_handle_.promise()).exception();
        if (exception) [[unlikely]] {
            Base::task_handle_.destroy();
            std::rethrow_exception(exception);
        }
        T value = std::move(Base::task_handle_.promise()).value();
        Base::task_handle_.destroy();
        return value;
    }
};

template <typename Allocator>
struct TaskAwaiter<void, Allocator> : public TaskAwaiterBase<void, Allocator> {
    using Base = TaskAwaiterBase<void, Allocator>;
    using Base::Base;

    void await_resume() {
        Context::current().runtime()->resume_work_internal();
        auto exception = std::move(Base::task_handle_.promise()).exception();
        Base::task_handle_.destroy();
        if (exception) [[unlikely]] {
            std::rethrow_exception(exception);
        }
    }
};

template <typename T, typename Allocator>
inline auto TaskBase<T, Allocator>::operator co_await() noexcept {
    return TaskAwaiter<T, Allocator>(std::exchange(handle_, nullptr),
                                     Context::current().runtime());
}

} // namespace detail
} // namespace condy