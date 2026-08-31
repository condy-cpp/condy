/**
 * @file senders.hpp
 * @brief Sender types for composing asynchronous operations.
 */

#pragma once

#include <version>

#include "condy/detail/senders.hpp"
#if defined(__cpp_lib_senders)
#include "condy/detail/execution.hpp"
#endif

namespace condy {

#if defined(__cpp_lib_senders)

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler>
using OpSender = detail::StandardSender<detail::OpSender<PrepFunc, CQEHandler>>;

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler,
          typename MultiShotFunc>
using MultiShotOpSender = detail::StandardSender<
    detail::MultiShotOpSender<PrepFunc, CQEHandler, MultiShotFunc>>;

template <PrepFuncLike PrepFunc, CQEHandlerLike CQEHandler, typename FreeFunc>
using ZeroCopyOpSender = detail::StandardSender<
    detail::ZeroCopyOpSender<PrepFunc, CQEHandler, FreeFunc>>;

template <unsigned int Flags, typename Sender>
using FlaggedOpSender =
    detail::StandardSender<detail::FlaggedOpSender<Flags, Sender>>;

template <typename... Senders>
using ParallelAllSender =
    detail::StandardSender<detail::ParallelAllSender<Senders...>>;

template <typename... Senders>
using ParallelAnySender =
    detail::StandardSender<detail::ParallelAnySender<Senders...>>;

template <typename... Senders>
using WhenAllSender = detail::StandardSender<detail::WhenAllSender<Senders...>>;

template <typename... Senders>
using WhenAnySender = detail::StandardSender<detail::WhenAnySender<Senders...>>;

template <typename... Senders>
using LinkSender = detail::StandardSender<detail::LinkSender<Senders...>>;

template <typename... Senders>
using HardLinkSender =
    detail::StandardSender<detail::HardLinkSender<Senders...>>;

template <typename Sender>
using RangedParallelAllSender =
    detail::StandardSender<detail::RangedParallelAllSender<Sender>>;

template <typename Sender>
using RangedParallelAnySender =
    detail::StandardSender<detail::RangedParallelAnySender<Sender>>;

template <typename Sender>
using RangedWhenAllSender =
    detail::StandardSender<detail::RangedWhenAllSender<Sender>>;

template <typename Sender>
using RangedWhenAnySender =
    detail::StandardSender<detail::RangedWhenAnySender<Sender>>;

template <typename Sender>
using RangedLinkSender =
    detail::StandardSender<detail::RangedLinkSender<Sender>>;

template <typename Sender>
using RangedHardLinkSender =
    detail::StandardSender<detail::RangedHardLinkSender<Sender>>;

#else

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

#endif

} // namespace condy