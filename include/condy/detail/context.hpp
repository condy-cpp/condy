/**
 * @file context.hpp
 */

#pragma once

#include "condy/detail/singleton.hpp"
#include <cassert>
#include <cstdint>

namespace condy {

class Ring;
class Runtime;

namespace detail {

template <typename T, T From = 0, T To = std::numeric_limits<T>::max()>
class IdPool {
public:
    static_assert(From < To, "Invalid ID range");

    T allocate() {
        if (!recycled_ids_.empty()) {
            T id = recycled_ids_.top();
            recycled_ids_.pop();
            return id;
        }
        if (next_id_ < To) {
            return next_id_++;
        }
        throw std::runtime_error("ID pool exhausted");
    }

    void recycle(T id) noexcept {
        assert(From <= id && id < next_id_ && id < To);
        recycled_ids_.push(id);
    }

    void reset() noexcept {
        next_id_ = From;
        while (!recycled_ids_.empty()) {
            recycled_ids_.pop();
        }
    }

private:
    T next_id_ = From;
    std::stack<T> recycled_ids_;
};

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