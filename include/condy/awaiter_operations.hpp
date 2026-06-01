/**
 * @file awaiter_operations.hpp
 * @brief Helper functions for composing asynchronous operations.
 * @details This file provides a set of interfaces for constructing asynchronous
 * operations. It includes utilities for wrapping liburing interfaces as
 * awaitable asynchronous operations, as well as for composing and combining
 * these operations to enable collective execution.
 */

#pragma once

#include "condy/concepts.hpp"
#include "condy/sender_operations.hpp"

namespace condy {

/**
 * @brief Build a single-shot operation awaiter with custom CQE handler.
 * @tparam CQEHandler Type of CQE handler.
 * @tparam PrepFunc Type of preparation function.
 * @tparam Args Additional arguments for CQE handler construction.
 * @param func Preparation function that accepts `Ring*` and returns
 * `io_uring_sqe*`.
 * @param handler_args Arguments forwarded to CQE handler constructor.
 * @return OpAwaiter The constructed awaiter.
 */
template <CQEHandlerLike CQEHandler, PrepFuncLike PrepFunc, typename... Args>
auto build_op_awaiter(PrepFunc &&func, Args &&...handler_args) {
    return build_op_sender<CQEHandler>(std::forward<PrepFunc>(func),
                                       std::forward<Args>(handler_args)...);
}

/**
 * @brief Build a multi-shot operation awaiter with custom CQE handler.
 * @tparam CQEHandler Type of CQE handler.
 * @tparam PrepFunc Type of preparation function.
 * @tparam MultiShotFunc Type of callback function for multi-shot operations.
 * @tparam Args Additional arguments for CQE handler construction.
 * @param func Preparation function that accepts `Ring*` and returns
 * `io_uring_sqe*`.
 * @param multishot_func Callback invoked on each completion except the last
 * one.
 * @param handler_args Arguments forwarded to CQE handler constructor.
 * @return MultiShotOpAwaiter The constructed awaiter.
 */
template <CQEHandlerLike CQEHandler, PrepFuncLike PrepFunc,
          typename MultiShotFunc, typename... Args>
auto build_multishot_op_awaiter(PrepFunc &&func, MultiShotFunc &&multishot_func,
                                Args &&...handler_args) {
    return build_multishot_op_sender<CQEHandler>(
        std::forward<PrepFunc>(func),
        std::forward<MultiShotFunc>(multishot_func),
        std::forward<Args>(handler_args)...);
}

/**
 * @brief Build a zero-copy operation awaiter with custom CQE handler.
 * @tparam CQEHandler Type of CQE handler.
 * @tparam PrepFunc Type of preparation function.
 * @tparam FreeFunc Type of resource cleanup function.
 * @tparam Args Additional arguments for CQE handler construction.
 * @param func Preparation function that accepts `Ring*` and returns
 * `io_uring_sqe*`.
 * @param free_func Cleanup function invoked when resource no longer needed.
 * @param handler_args Arguments forwarded to CQE handler constructor.
 * @return ZeroCopyOpAwaiter The constructed awaiter.
 */
template <CQEHandlerLike CQEHandler, PrepFuncLike PrepFunc, typename FreeFunc,
          typename... Args>
auto build_zero_copy_op_awaiter(PrepFunc &&func, FreeFunc &&free_func,
                                Args &&...handler_args) {
    return build_zero_copy_op_sender<CQEHandler>(
        std::forward<PrepFunc>(func), std::forward<FreeFunc>(free_func),
        std::forward<Args>(handler_args)...);
}

} // namespace condy