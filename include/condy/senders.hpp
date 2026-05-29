/**
 * @file senders.hpp
 * @brief Sender types for composing asynchronous operations.
 */

#pragma once

#include "condy/concepts.hpp"
#include "condy/op_states.hpp"
#include <array>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace condy {

namespace detail {

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler,
          template <typename...> class FinishHandle, typename... Args>
class [[nodiscard]] OpSenderBase {
public:
    using CondySender = void;
    using ReturnType = std::invoke_result_t<CQEHandler &, io_uring_cqe *>;

    OpSenderBase(PrepFunc func, CQEHandler cqe_handler, Args... args)
        : prep_func_(std::move(func)), cqe_handler_(std::move(cqe_handler)),
          args_(std::make_tuple(std::move(args)...)) {}

    template <typename Receiver> auto connect_impl(Receiver receiver) noexcept {
        return std::apply(
            [&](auto &&...args) {
                return detail::OpSenderOperationState<
                    FinishHandle<CQEHandler, Args..., Receiver>, PrepFunc>(
                    std::move(prep_func_), std::move(cqe_handler_),
                    std::move(receiver), std::forward<decltype(args)>(args)...);
            },
            std::move(args_));
    }

private:
    PrepFunc prep_func_;
    CQEHandler cqe_handler_;
    std::tuple<Args...> args_;
};

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler>
using OpSender = OpSenderBase<PrepFunc, CQEHandler, detail::OpFinishHandle>;

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler,
          typename MultiShotFunc>
using MultiShotOpSender =
    OpSenderBase<PrepFunc, CQEHandler, detail::MultiShotOpFinishHandle,
                 MultiShotFunc>;

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler, typename FreeFunc>
using ZeroCopyOpSender = OpSenderBase<PrepFunc, CQEHandler,
                                      detail::ZeroCopyOpFinishHandle, FreeFunc>;

template <unsigned int Flags, typename Sender>
class [[nodiscard]] FlaggedOpSender {
public:
    using CondySender = void;
    using ReturnType = typename Sender::ReturnType;

    FlaggedOpSender(Sender sender) : sender_(std::move(sender)) {}

    template <typename Receiver> auto connect_impl(Receiver receiver) noexcept {
        return detail::FlaggedOpState<Flags, Sender, Receiver>(
            std::move(sender_), std::move(receiver));
    }

private:
    Sender sender_;
};

template <template <typename, unsigned int, typename...> class OperationState,
          unsigned int Flags>
struct link_sender_helper {
    template <typename Receiver, typename... Senders>
    using apply = OperationState<Receiver, Flags, Senders...>;
};

template <typename Return, template <typename...> class OperationState,
          typename... Senders>
class [[nodiscard]] ParallelSenderBase {
public:
    using CondySender = void;
    using ReturnType = Return;

    ParallelSenderBase(Senders... senders) : senders_(std::move(senders)...) {}

    template <typename PrevReturn, typename S, typename... Ss>
    ParallelSenderBase(
        ParallelSenderBase<PrevReturn, OperationState, Ss...> &&other, S sender)
        : senders_(std::tuple_cat(std::move(other.senders_),
                                  std::make_tuple(std::move(sender)))) {}

    template <typename Receiver> auto connect_impl(Receiver receiver) noexcept {
        return OperationState<Receiver, Senders...>(std::move(senders_),
                                                    std::move(receiver));
    }

private:
    std::tuple<Senders...> senders_;

    template <typename, template <typename...> class, typename...>
    friend class ParallelSenderBase;
};

template <typename... Senders>
using ParallelAllSender =
    ParallelSenderBase<std::pair<std::array<size_t, sizeof...(Senders)>,
                                 std::tuple<typename Senders::ReturnType...>>,
                       detail::ParallelAllOperationState, Senders...>;

template <typename... Senders>
using ParallelAnySender =
    ParallelSenderBase<std::pair<std::array<size_t, sizeof...(Senders)>,
                                 std::tuple<typename Senders::ReturnType...>>,
                       detail::ParallelAnyOperationState, Senders...>;

template <typename... Senders>
using WhenAllSender =
    ParallelSenderBase<std::tuple<typename Senders::ReturnType...>,
                       detail::WhenAllOperationState, Senders...>;

template <typename... Senders>
using WhenAnySender =
    ParallelSenderBase<std::variant<typename Senders::ReturnType...>,
                       detail::WhenAnyOperationState, Senders...>;

template <unsigned int Flags, typename... Senders>
using LinkSenderBase =
    ParallelSenderBase<std::tuple<typename Senders::ReturnType...>,
                       detail::link_sender_helper<detail::LinkOperationState,
                                                  Flags>::template apply,
                       Senders...>;

template <typename... Senders>
using LinkSender = LinkSenderBase<IOSQE_IO_LINK, Senders...>;

template <typename... Senders>
using HardLinkSender = LinkSenderBase<IOSQE_IO_HARDLINK, Senders...>;

template <typename Return, template <typename...> class OperationState,
          typename Sender>
class [[nodiscard]] RangedParallelSenderBase {
public:
    using CondySender = void;
    using ReturnType = Return;

    RangedParallelSenderBase(std::vector<Sender> senders)
        : senders_(std::move(senders)) {}

    template <typename Receiver> auto connect_impl(Receiver receiver) noexcept {
        return OperationState<Receiver, Sender>(std::move(senders_),
                                                std::move(receiver));
    }

private:
    std::vector<Sender> senders_;
};

template <typename Sender>
using RangedParallelAllSender = RangedParallelSenderBase<
    std::pair<std::vector<size_t>, std::vector<typename Sender::ReturnType>>,
    detail::RangedParallelAllOperationState, Sender>;

template <typename Sender>
using RangedParallelAnySender = RangedParallelSenderBase<
    std::pair<std::vector<size_t>, std::vector<typename Sender::ReturnType>>,
    detail::RangedParallelAnyOperationState, Sender>;

template <typename Sender>
using RangedWhenAllSender =
    RangedParallelSenderBase<std::vector<typename Sender::ReturnType>,
                             detail::RangedWhenAllOperationState, Sender>;

template <typename Sender>
using RangedWhenAnySender =
    RangedParallelSenderBase<std::pair<size_t, typename Sender::ReturnType>,
                             detail::RangedWhenAnyOperationState, Sender>;

template <unsigned int Flags, typename Sender>
using RangedLinkSenderBase = RangedParallelSenderBase<
    std::vector<typename Sender::ReturnType>,
    detail::link_sender_helper<detail::RangedLinkOperationState,
                               Flags>::template apply,
    Sender>;

template <typename Sender>
using RangedLinkSender = RangedLinkSenderBase<IOSQE_IO_LINK, Sender>;

template <typename Sender>
using RangedHardLinkSender = RangedLinkSenderBase<IOSQE_IO_HARDLINK, Sender>;

} // namespace detail

// TODO: This re-export is intentional. We may adapt these senders to standard
// sender/receiver concepts in the future.
using detail::FlaggedOpSender;
using detail::HardLinkSender;
using detail::LinkSender;
using detail::MultiShotOpSender;
using detail::OpSender;
using detail::ParallelAllSender;
using detail::ParallelAnySender;
using detail::RangedHardLinkSender;
using detail::RangedLinkSender;
using detail::RangedParallelAllSender;
using detail::RangedParallelAnySender;
using detail::RangedWhenAllSender;
using detail::RangedWhenAnySender;
using detail::WhenAllSender;
using detail::WhenAnySender;
using detail::ZeroCopyOpSender;

} // namespace condy