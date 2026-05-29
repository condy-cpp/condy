/**
 * @file context.hpp
 */

#pragma once

#include "condy/singleton.hpp"
#include "condy/utils.hpp"
#include <cassert>
#include <cstdint>

namespace condy {

class Ring;
class Runtime;

namespace detail {

class Context : public ThreadLocalSingleton<Context> {
public:
    void init(Runtime *runtime) noexcept {
        runtime_ = runtime;
        bgid_pool_.reset();
    }
    void reset() noexcept {
        runtime_ = nullptr;
        bgid_pool_.reset();
    }

    Runtime *runtime() noexcept { return runtime_; }

    uint16_t next_bgid() { return bgid_pool_.allocate(); }

    void recycle_bgid(uint16_t bgid) noexcept { bgid_pool_.recycle(bgid); }

private:
    Runtime *runtime_ = nullptr;
    IdPool<uint16_t> bgid_pool_;
};

} // namespace detail

} // namespace condy