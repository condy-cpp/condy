/**
 * @file singleton.hpp
 */

#pragma once

#include "condy/utils.hpp"

namespace condy {
namespace detail {

template <typename T> class ThreadLocalSingleton {
public:
    ThreadLocalSingleton() = default;

    CONDY_DELETE_COPY_MOVE(ThreadLocalSingleton);

    static T &current() noexcept {
        static thread_local T instance;
        return instance;
    }
};

} // namespace detail
} // namespace condy