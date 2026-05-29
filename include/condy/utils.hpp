/**
 * @file utils.hpp
 * @brief Internal utility classes and functions used by Condy.
 */

#pragma once

#include "condy/detail/utils.hpp"

namespace condy {

template <typename Func> class [[nodiscard]] Defer {
public:
    Defer(Func func) : func_(std::move(func)) {}
    ~Defer() {
        if (active_)
            func_();
    }

    CONDY_DELETE_COPY_MOVE(Defer);

public:
    void dismiss() noexcept { active_ = false; }

private:
    Func func_;
    bool active_ = true;
};

/**
 * @brief Defer the execution of a function until the current scope ends.
 * @param func The function to be executed upon scope exit.
 * @return Defer object that will execute the function when it goes out of
 * scope.
 */
template <typename Func> auto defer(Func &&func) {
    return Defer<std::decay_t<Func>>(std::forward<Func>(func));
}

} // namespace condy
