/**
 * @file senders.hpp
 * @brief Sender types for composing asynchronous operations.
 */

#pragma once

#include "condy/detail/senders.hpp"

namespace condy {

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