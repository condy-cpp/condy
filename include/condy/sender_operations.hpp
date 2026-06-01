/**
 * @file sender_operations.hpp
 * @brief Helper functions for composing asynchronous operations.
 */

#pragma once

#include "condy/concepts.hpp"
#include "condy/senders.hpp"
#include <stdexcept>

namespace condy {

template <CQEHandlerLike CQEHandler, PrepFuncLike PrepFunc, typename... Args>
auto build_op_sender(PrepFunc &&prep_func, Args &&...args) {
    return OpSender<std::decay_t<PrepFunc>, CQEHandler>(
        std::forward<PrepFunc>(prep_func),
        CQEHandler(std::forward<Args>(args)...));
}

template <CQEHandlerLike CQEHandler, PrepFuncLike PrepFunc,
          typename MultiShotFunc, typename... Args>
auto build_multishot_op_sender(PrepFunc &&func, MultiShotFunc &&multishot_func,
                               Args &&...handler_args) {
    return MultiShotOpSender<std::decay_t<PrepFunc>, CQEHandler,
                             std::decay_t<MultiShotFunc>>(
        std::forward<PrepFunc>(func),
        CQEHandler(std::forward<Args>(handler_args)...),
        std::forward<MultiShotFunc>(multishot_func));
}

template <CQEHandlerLike CQEHandler, PrepFuncLike PrepFunc, typename FreeFunc,
          typename... Args>
auto build_zero_copy_op_sender(PrepFunc &&func, FreeFunc &&free_func,
                               Args &&...handler_args) {
    return ZeroCopyOpSender<std::decay_t<PrepFunc>, CQEHandler,
                            std::decay_t<FreeFunc>>(
        std::forward<PrepFunc>(func),
        CQEHandler(std::forward<Args>(handler_args)...),
        std::forward<FreeFunc>(free_func));
}

/**
 * @brief Decorates an operation with specific io_uring sqe flags.
 * @tparam Flags The io_uring sqe flags to set.
 * @param sender The operation to decorate.
 */
template <unsigned int Flags, typename Sender> auto flag(Sender &&sender) {
    return detail::FlaggedOpSender<Flags, std::decay_t<Sender>>(
        std::forward<Sender>(sender));
}

/**
 * @brief Mark an operation as drain operation.
 * @param sender The operation to mark as drain.
 */
template <typename Sender> auto drain(Sender &&sender) {
    return flag<IOSQE_IO_DRAIN>(std::forward<Sender>(sender));
}

/**
 * @brief Mark an operation to always execute asynchronously.
 * @param sender The operation to mark as always async.
 */
template <typename Sender> auto always_async(Sender &&sender) {
    return flag<IOSQE_ASYNC>(std::forward<Sender>(sender));
}

/**
 * @brief Compose multiple operations into a single sender that executes them in
 * parallel.
 * @tparam SenderType The type of sender to compose into.
 * @param senders The operations to compose.
 */
template <template <typename... Senders> typename SenderType,
          typename... Senders>
auto parallel(Senders &&...senders) {
    return SenderType<std::decay_t<Senders>...>(
        std::forward<Senders>(senders)...);
}

/**
 * @brief Compose multiple operations from a range into a single operation that
 * executes them in parallel.
 * @tparam RangedSenderType The type of sender to compose into.
 * @param range The range of operations to compose.
 */
template <template <typename Sender> typename RangedSenderType,
          std::ranges::range Range>
auto parallel(Range &&range) {
    using SenderType = typename std::remove_cvref_t<Range>::value_type;
    auto begin = std::make_move_iterator(std::begin(range));
    auto end = std::make_move_iterator(std::end(range));
    std::vector<SenderType> senders(begin, end);
    return RangedSenderType<SenderType>(std::move(senders));
}

/**
 * @brief Compose multiple operations into a single operation that completes
 * when all of them complete.
 * @param senders The operations to compose.
 */
template <typename... Senders> auto when_all(Senders &&...senders) {
    return parallel<WhenAllSender>(std::forward<Senders>(senders)...);
}

/**
 * @brief Compose multiple operations from a range into a single operation that
 * completes when all of them complete.
 * @param range The range of operations to compose.
 */
template <std::ranges::range Range> auto when_all(Range &&range) {
    return parallel<RangedWhenAllSender>(std::forward<Range>(range));
}

/**
 * @brief Compose multiple operations into a single operation that completes
 * when any of them complete.
 * @param senders The operations to compose.
 */
template <typename... Senders> auto when_any(Senders &&...senders) {
    static_assert(sizeof...(Senders) > 0,
                  "when_any requires at least one sender");
    return parallel<WhenAnySender>(std::forward<Senders>(senders)...);
}

/**
 * @brief Compose multiple operations from a range into a single operation that
 * completes when any of them complete.
 * @param range The range of operations to compose.
 */
template <std::ranges::range Range> auto when_any(Range &&range) {
    if (std::ranges::empty(range)) {
        throw std::invalid_argument("when_any requires at least one sender");
    }
    return parallel<RangedWhenAnySender>(std::forward<Range>(range));
}

/**
 * @brief Compose multiple operations into a single operation that executes them
 * in sequence.
 * @param senders The operations to compose.
 */
template <typename... Senders> auto link(Senders &&...senders) {
    return parallel<LinkSender>(std::forward<Senders>(senders)...);
}

/**
 * @brief Compose multiple operations from a range into a single operation that
 * executes them in sequence.
 * @param range The range of operations to compose.
 */
template <std::ranges::range Range> auto link(Range &&range) {
    return parallel<RangedLinkSender>(std::forward<Range>(range));
}

/**
 * @brief Compose multiple operations into a single operation that executes them
 * in sequence and continues even if one of them fails.
 * @param senders The operations to compose.
 */
template <typename... Senders> auto hard_link(Senders &&...senders) {
    return parallel<HardLinkSender>(std::forward<Senders>(senders)...);
}

/**
 * @brief Compose multiple operations from a range into a single operation that
 * executes them in sequence and continues even if one of them fails.
 * @param range The range of operations to compose.
 */
template <std::ranges::range Range> auto hard_link(Range &&range) {
    return parallel<RangedHardLinkSender>(std::forward<Range>(range));
}

/**
 * @brief Operators for composing operations.
 */
namespace operators {

/**
 * @brief Operator overloads version of condy::when_all
 */
template <typename Sender1, typename Sender2>
auto operator&&(Sender1 s1, Sender2 s2) {
    return when_all(std::move(s1), std::move(s2));
}

/**
 * @brief Operator overloads version of condy::when_all
 */
template <typename S, typename... Ss>
auto operator&&(WhenAllSender<Ss...> aws, S sender) {
    return WhenAllSender<Ss..., std::decay_t<S>>(std::move(aws),
                                                 std::move(sender));
}

/**
 * @brief Operator overloads version of condy::when_any
 */
template <typename Sender1, typename Sender2>
auto operator||(Sender1 s1, Sender2 s2) {
    return when_any(std::move(s1), std::move(s2));
}

/**
 * @brief Operator overloads version of condy::when_any
 */
template <typename S, typename... Ss>
auto operator||(WhenAnySender<Ss...> aws, S sender) {
    return WhenAnySender<Ss..., std::decay_t<S>>(std::move(aws),
                                                 std::move(sender));
}

/**
 * @brief Operator overloads version of condy::link
 */
template <typename Sender1, typename Sender2>
auto operator>>(Sender1 s1, Sender2 s2) {
    return link(std::move(s1), std::move(s2));
}

/**
 * @brief Operator overloads version of condy::link
 */
template <typename S, typename... Ss>
auto operator>>(LinkSender<Ss...> aws, S sender) {
    return LinkSender<Ss..., std::decay_t<S>>(std::move(aws),
                                              std::move(sender));
}

} // namespace operators

} // namespace condy