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
 * @tparam PrepFunc Type of preparation function.
 * @tparam CQEHandler Type of CQE handler.
 * @param func Preparation function that accepts `Ring*` and returns
 * `io_uring_sqe*`.
 * @param cqe_handler CQE handler instance.
 * @return OpAwaiter The constructed awaiter.
 */
template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler>
auto build_op_awaiter(PrepFunc &&func, CQEHandler &&cqe_handler) {
    return build_op_sender(std::forward<PrepFunc>(func),
                           std::forward<CQEHandler>(cqe_handler));
}

/**
 * @brief Build a multi-shot operation awaiter with custom CQE handler.
 * @tparam PrepFunc Type of preparation function.
 * @tparam CQEHandler Type of CQE handler.
 * @tparam MultiShotFunc Type of callback function for multi-shot operations.
 * @param func Preparation function that accepts `Ring*` and returns
 * `io_uring_sqe*`.
 * @param cqe_handler CQE handler instance.
 * @param multishot_func Callback invoked on each completion except the last
 * one.
 * @return MultiShotOpAwaiter The constructed awaiter.
 */
template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler,
          typename MultiShotFunc>
auto build_multishot_op_awaiter(PrepFunc &&func, CQEHandler &&cqe_handler,
                                MultiShotFunc &&multishot_func) {
    return build_multishot_op_sender(
        std::forward<PrepFunc>(func), std::forward<CQEHandler>(cqe_handler),
        std::forward<MultiShotFunc>(multishot_func));
}

/**
 * @brief Build a zero-copy operation awaiter with custom CQE handler.
 * @tparam PrepFunc Type of preparation function.
 * @tparam CQEHandler Type of CQE handler.
 * @tparam FreeFunc Type of resource cleanup function.
 * @param func Preparation function that accepts `Ring*` and returns
 * `io_uring_sqe*`.
 * @param cqe_handler CQE handler instance.
 * @param free_func Cleanup function invoked when resource no longer needed.
 * @return ZeroCopyOpAwaiter The constructed awaiter.
 */
template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler, typename FreeFunc>
auto build_zero_copy_op_awaiter(PrepFunc &&func, CQEHandler &&cqe_handler,
                                FreeFunc &&free_func) {
    return build_zero_copy_op_sender(std::forward<PrepFunc>(func),
                                     std::forward<CQEHandler>(cqe_handler),
                                     std::forward<FreeFunc>(free_func));
}

} // namespace condy
