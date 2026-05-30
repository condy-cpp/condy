/**
 * @file utils.hpp
 * @brief Internal utility classes and functions used by Condy.
 */

#pragma once

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <iostream>
#include <limits>
#include <new>
#include <stack>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

// NOLINTBEGIN(bugprone-macro-parentheses)
#define CONDY_DELETE_COPY(cls)                                                 \
    cls(const cls &) = delete;                                                 \
    cls &operator=(const cls &) = delete

#define CONDY_DELETE_MOVE(cls)                                                 \
    cls(cls &&) = delete;                                                      \
    cls &operator=(cls &&) = delete
// NOLINTEND(bugprone-macro-parentheses)

#define CONDY_DELETE_COPY_MOVE(cls)                                            \
    CONDY_DELETE_COPY(cls);                                                    \
    CONDY_DELETE_MOVE(cls)

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define CONDY_DETAIL_HAS_TSAN
#endif
#endif

#if defined(__SANITIZE_THREAD__)
#define CONDY_DETAIL_HAS_TSAN
#endif

#if defined(CONDY_DETAIL_HAS_TSAN)
extern "C" {
void __tsan_acquire(void *addr); // NOLINT(bugprone-reserved-identifier)
void __tsan_release(void *addr); // NOLINT(bugprone-reserved-identifier)
}
#endif

namespace condy {
namespace detail {

inline void tsan_acquire([[maybe_unused]] void *addr) noexcept {
#if defined(CONDY_DETAIL_HAS_TSAN)
    __tsan_acquire(addr);
#endif
}

inline void tsan_release([[maybe_unused]] void *addr) noexcept {
#if defined(CONDY_DETAIL_HAS_TSAN)
    __tsan_release(addr);
#endif
}

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

[[noreturn]] inline void panic_on(std::string_view msg) noexcept {
    std::cerr << std::format("Panic: {}\n", msg);
#ifndef CRASH_TEST
    std::terminate();
#else
    // Ctest cannot handle SIGABRT, so we use exit here
    std::exit(EXIT_FAILURE);
#endif
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

inline auto make_system_error(std::string_view msg, int ec) {
    return std::system_error(ec, std::generic_category(), std::string(msg));
}

inline auto make_system_error(std::string_view msg) {
    return make_system_error(msg, errno);
}

#if __cplusplus >= 202302L
[[noreturn]] inline void unreachable() { std::unreachable(); }
#else
[[noreturn]] inline void unreachable() { __builtin_unreachable(); }
#endif

template <size_t Idx = 0, typename... Ts>
std::variant<Ts...> tuple_at(std::tuple<Ts...> &results, size_t idx) {
    if constexpr (Idx < sizeof...(Ts)) {
        if (idx == Idx) {
            return std::variant<Ts...>{std::in_place_index<Idx>,
                                       std::move(std::get<Idx>(results))};
        } else {
            return tuple_at<Idx + 1, Ts...>(results, idx);
        }
    } else {
#ifdef __clang__
        // Should not reach here, but clang can misoptimize this path if we
        // mark it as unreachable. Confirmed fixed in clang 20.1.8, but the
        // exact cause was not investigated.
        assert(false && "Index out of bounds");
        return std::variant<Ts...>{std::in_place_index<0>,
                                   std::move(std::get<0>(results))};
#else
        panic_on("Index out of bounds in tuple_at");
#endif
    }
}

template <typename T> inline T align_up(T value, T alignment) noexcept {
    // alignment must be a power of two
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0);
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace detail
} // namespace condy

#undef CONDY_DETAIL_HAS_TSAN
