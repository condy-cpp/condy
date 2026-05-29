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

template <typename T> class RawStorage {
public:
    template <typename Factory>
    void accept(Factory &&factory) noexcept(
        noexcept(T(std::forward<Factory>(factory)()))) {
        new (&storage_) T(std::forward<Factory>(factory)());
    }

    template <typename... Args>
    void construct(Args &&...args) noexcept(
        std::is_nothrow_constructible_v<T, Args...>) {
        accept([&]() { return T(std::forward<Args>(args)...); });
    }

    T &get() noexcept { return *std::launder(reinterpret_cast<T *>(storage_)); }

    const T &get() const noexcept {
        return *std::launder(reinterpret_cast<const T *>(storage_));
    }

    void destroy() noexcept { get().~T(); }

private:
    alignas(T) unsigned char storage_[sizeof(T)];
};

template <typename T, size_t N> class SmallArray {
public:
    SmallArray(size_t capacity) : capacity_(capacity) {
        if (!is_small_()) {
            large_ = new T[capacity];
        }
    }

    ~SmallArray() {
        if (!is_small_()) {
            delete[] large_;
        }
    }

    T &operator[](size_t index) noexcept {
        return is_small_() ? small_[index] : large_[index];
    }

    const T &operator[](size_t index) const noexcept {
        return is_small_() ? small_[index] : large_[index];
    }

    size_t capacity() const noexcept { return capacity_; }

private:
    bool is_small_() const noexcept { return capacity_ <= N; }

private:
    size_t capacity_;
    union {
        T small_[N];
        T *large_;
    };
};

} // namespace condy
