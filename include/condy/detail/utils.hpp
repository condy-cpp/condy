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

[[noreturn]] inline void panic_on(std::string_view msg) noexcept {
    std::cerr << std::format("Panic: {}\n", msg);
#ifndef CRASH_TEST
    std::terminate();
#else
    // Ctest cannot handle SIGABRT, so we use exit here
    std::exit(EXIT_FAILURE);
#endif
}

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

} // namespace detail
} // namespace condy

#undef CONDY_DETAIL_HAS_TSAN
