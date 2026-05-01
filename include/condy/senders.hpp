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

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler>
class [[nodiscard]] OpSender {
public:
    using ReturnType = std::invoke_result_t<CQEHandler &, io_uring_cqe *>;

    OpSender(PrepFunc func, CQEHandler cqe_handler)
        : prep_func_(std::move(func)), cqe_handler_(std::move(cqe_handler)) {}

    template <typename Receiver> auto connect(Receiver receiver) noexcept {
        return detail::OpSenderOperationState<
            OpFinishHandle<CQEHandler, Receiver>, PrepFunc>(
            std::move(prep_func_), std::move(cqe_handler_),
            std::move(receiver));
    }

private:
    PrepFunc prep_func_;
    CQEHandler cqe_handler_;
};

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler,
          typename MultiShotFunc>
class [[nodiscard]] MultiShotOpSender {
public:
    using ReturnType = std::invoke_result_t<CQEHandler &, io_uring_cqe *>;

    MultiShotOpSender(PrepFunc func, CQEHandler cqe_handler,
                      MultiShotFunc multi_shot_func)
        : prep_func_(std::move(func)), cqe_handler_(std::move(cqe_handler)),
          multi_shot_func_(std::move(multi_shot_func)) {}

    template <typename Receiver> auto connect(Receiver receiver) noexcept {
        return detail::OpSenderOperationState<
            MultiShotOpFinishHandle<CQEHandler, MultiShotFunc, Receiver>,
            PrepFunc>(std::move(prep_func_), std::move(cqe_handler_),
                      std::move(receiver), std::move(multi_shot_func_));
    }

private:
    PrepFunc prep_func_;
    CQEHandler cqe_handler_;
    MultiShotFunc multi_shot_func_;
};

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler, typename FreeFunc>
class [[nodiscard]] ZeroCopyOpSender {
public:
    using ReturnType = std::invoke_result_t<CQEHandler &, io_uring_cqe *>;

    ZeroCopyOpSender(PrepFunc func, CQEHandler cqe_handler, FreeFunc free_func)
        : prep_func_(std::move(func)), cqe_handler_(std::move(cqe_handler)),
          free_func_(std::move(free_func)) {}

    template <typename Receiver> auto connect(Receiver receiver) noexcept {
        return detail::OpSenderOperationState<
            ZeroCopyOpFinishHandle<CQEHandler, FreeFunc, Receiver>, PrepFunc>(
            std::move(prep_func_), std::move(cqe_handler_), std::move(receiver),
            std::move(free_func_));
    }

private:
    PrepFunc prep_func_;
    CQEHandler cqe_handler_;
    FreeFunc free_func_;
};

template <unsigned int Flags, typename Sender>
class [[nodiscard]] FlaggedOpSender {
public:
    using ReturnType = typename Sender::ReturnType;

    FlaggedOpSender(Sender sender) : sender_(std::move(sender)) {}

    template <typename Receiver> auto connect(Receiver receiver) noexcept {
        return detail::FlaggedOpState<Flags, Sender, Receiver>(
            std::move(sender_), std::move(receiver));
    }

private:
    Sender sender_;
};

namespace detail {

template <template <typename, unsigned int, typename...> class OperationState,
          unsigned int Flags>
struct link_sender_helper {
    template <typename Receiver, typename... Senders>
    using apply = OperationState<Receiver, Flags, Senders...>;
};

} // namespace detail

template <typename Return, template <typename...> class OperationState,
          typename... Senders>
class [[nodiscard]] ParallelSender {
public:
    using ReturnType = Return;

    ParallelSender(Senders... senders) : senders_(std::move(senders)...) {}

    template <typename PrevReturn, typename S, typename... Ss>
    ParallelSender(ParallelSender<PrevReturn, OperationState, Ss...> &&other,
                   S sender)
        : senders_(std::tuple_cat(std::move(other.senders_),
                                  std::make_tuple(std::move(sender)))) {}

    template <typename Receiver> auto connect(Receiver receiver) noexcept {
        return OperationState<Receiver, Senders...>(std::move(senders_),
                                                    std::move(receiver));
    }

private:
    std::tuple<Senders...> senders_;

    template <typename, template <typename...> class, typename...>
    friend class ParallelSender;
};

template <typename... Senders>
using ParallelAllSender =
    ParallelSender<std::pair<std::array<size_t, sizeof...(Senders)>,
                             std::tuple<typename Senders::ReturnType...>>,
                   detail::ParallelAllOperationState, Senders...>;

template <typename... Senders>
using ParallelAnySender =
    ParallelSender<std::pair<std::array<size_t, sizeof...(Senders)>,
                             std::tuple<typename Senders::ReturnType...>>,
                   detail::ParallelAnyOperationState, Senders...>;

template <typename... Senders>
using WhenAllSender =
    ParallelSender<std::tuple<typename Senders::ReturnType...>,
                   detail::WhenAllOperationState, Senders...>;

template <typename... Senders>
using WhenAnySender =
    ParallelSender<std::variant<typename Senders::ReturnType...>,
                   detail::WhenAnyOperationState, Senders...>;

template <unsigned int Flags, typename... Senders>
using LinkSenderBase =
    ParallelSender<std::tuple<typename Senders::ReturnType...>,
                   detail::link_sender_helper<detail::LinkOperationState,
                                              Flags>::template apply,
                   Senders...>;

template <typename... Senders>
using LinkSender = LinkSenderBase<IOSQE_IO_LINK, Senders...>;

template <typename... Senders>
using HardLinkSender = LinkSenderBase<IOSQE_IO_HARDLINK, Senders...>;

template <typename Return, template <typename...> class OperationState,
          typename Sender>
class [[nodiscard]] RangedParallelSender {
public:
    using ReturnType = Return;

    RangedParallelSender(std::vector<Sender> senders)
        : senders_(std::move(senders)) {}

    template <typename Receiver> auto connect(Receiver receiver) noexcept {
        return OperationState<Receiver, Sender>(std::move(senders_),
                                                std::move(receiver));
    }

private:
    std::vector<Sender> senders_;
};

template <typename Sender>
using RangedParallelAllSender = RangedParallelSender<
    std::pair<std::vector<size_t>, std::vector<typename Sender::ReturnType>>,
    detail::RangedParallelAllOperationState, Sender>;

template <typename Sender>
using RangedParallelAnySender = RangedParallelSender<
    std::pair<std::vector<size_t>, std::vector<typename Sender::ReturnType>>,
    detail::RangedParallelAnyOperationState, Sender>;

template <typename Sender>
using RangedWhenAllSender =
    RangedParallelSender<std::vector<typename Sender::ReturnType>,
                         detail::RangedWhenAllOperationState, Sender>;

template <typename Sender>
using RangedWhenAnySender =
    RangedParallelSender<std::pair<size_t, typename Sender::ReturnType>,
                         detail::RangedWhenAnyOperationState, Sender>;

template <unsigned int Flags, typename Sender>
using RangedLinkSenderBase = RangedParallelSender<
    std::vector<typename Sender::ReturnType>,
    detail::link_sender_helper<detail::RangedLinkOperationState,
                               Flags>::template apply,
    Sender>;

template <typename Sender>
using RangedLinkSender = RangedLinkSenderBase<IOSQE_IO_LINK, Sender>;

template <typename Sender>
using RangedHardLinkSender = RangedLinkSenderBase<IOSQE_IO_HARDLINK, Sender>;

} // namespace condy