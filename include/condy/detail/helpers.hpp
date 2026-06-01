/**
 * @file helpers.hpp
 * @brief Helper functions for asynchronous operations.
 * @details This file defines a set of helper functions primarily used in
 * conjunction with asynchronous operations to enhance their expressiveness and
 * usability.
 */

#pragma once

#include "condy/concepts.hpp"
#include <utility>

namespace condy {
namespace detail {

template <typename CoroFunc> struct SpawnHelper {
    void operator()(auto &&res) noexcept {
        co_spawn(func(std::forward<decltype(res)>(res))).detach();
    }
    CoroFunc func;
};

template <typename Channel> struct PushHelper {
    void operator()(auto &&res) noexcept {
        channel.force_push(std::forward<decltype(res)>(res));
    }
    Channel &channel;
};

struct FixedFd {
    int value;
    operator int() const { return value; }
};

template <typename T> struct FixedBuffer {
    T value;
    int buf_index;
};

} // namespace detail
} // namespace condy