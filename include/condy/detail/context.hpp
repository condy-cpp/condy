/**
 * @file context.hpp
 */

#pragma once

#include "condy/detail/singleton.hpp"

namespace condy {

class Runtime;

namespace detail {

class Context : public ThreadLocalSingleton<Context> {
public:
    void init(Runtime *runtime) noexcept { runtime_ = runtime; }
    void reset() noexcept { runtime_ = nullptr; }

    Runtime *runtime() noexcept { return runtime_; }

private:
    Runtime *runtime_ = nullptr;
};

} // namespace detail

} // namespace condy