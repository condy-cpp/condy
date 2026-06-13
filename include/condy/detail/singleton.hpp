/**
 * @file singleton.hpp
 */

#pragma once

#include "condy/detail/utils.hpp"

namespace condy {
namespace detail {

template <typename T> class ThreadLocalSingleton {
public:
    CONDY_DELETE_COPY_MOVE(ThreadLocalSingleton);

    static T &current() noexcept {
        static thread_local T instance;
        return instance;
    }

private:
    ThreadLocalSingleton() = default;

    friend T;
};

} // namespace detail
} // namespace condy