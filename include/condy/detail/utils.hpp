/**
 * @file utils.hpp
 * @brief Internal utility classes and functions used by Condy.
 */

#pragma once

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

} // namespace detail
} // namespace condy

#undef CONDY_DETAIL_HAS_TSAN
