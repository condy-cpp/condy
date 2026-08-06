/**
 * @file execution.hpp
 * @brief std::execution integration with condy's runtime.
 */

#pragma once

#ifdef CONDY_HAS_EXECUTION

#include "condy/detail/execution.hpp"
#include "condy/runtime.hpp"
#include <utility>

namespace condy {

/**
 * @brief A scheduler that schedules work onto a condy Runtime.
 * @details Wraps a condy::Runtime so it can be used as a standard
 * std::execution scheduler. Obtain an instance with `condy::get_scheduler()`.
 */
class Scheduler : public detail::Scheduler {
private:
    friend Scheduler get_scheduler(Runtime &);

    Scheduler(Runtime &runtime) : detail::Scheduler(runtime) {}
};

/**
 * @brief Get a scheduler for the given runtime.
 */
inline Scheduler get_scheduler(Runtime &runtime) { return Scheduler{runtime}; }

/**
 * @brief Convert a standard sender into a plain awaiter.
 * @details Lets a condy coroutine `co_await` any std::execution sender. The
 * result follows `ex::sync_wait`. When the sender completes, execution resumes
 * on the runtime that started this operation.
 * @param sender The std::execution sender to wait for.
 * @return Awaiter usable with `co_await` in condy coroutines.
 */
template <typename Sender> auto wait_sender(Sender &&sender) {
    auto &runtime = current_runtime();
    auto s = std::forward<Sender>(sender) | detail::ex::affine();
    return detail::WaitSenderAwaiter<decltype(s)>(std::move(s), runtime);
}

} // namespace condy

#endif