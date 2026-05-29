/**
 * @file async_operations.hpp
 * @brief Definitions of asynchronous operations.
 * @details This file defines a series of asynchronous operations, which are
 * wrappers around liburing operations. Generally, each async_* function
 * corresponds to a io_uring_prep_* function in liburing
 */

#pragma once

#include "condy/concepts.hpp"
#include "condy/condy_uring.hpp"
#include "condy/detail/helpers.hpp"

namespace condy {
namespace detail {

class BundledProvidedBufferQueue;
class BundledProvidedBufferPool;

template <typename Awaiter>
auto maybe_flag_fixed_fd(Awaiter &&op, const FixedFd &) {
    return flag<IOSQE_FIXED_FILE>(std::forward<Awaiter>(op));
}

template <typename Awaiter> auto maybe_flag_fixed_fd(Awaiter &&op, int) {
    return std::forward<Awaiter>(op);
}

template <typename Fd>
constexpr bool is_fixed_fd_v = std::is_same_v<std::remove_cvref_t<Fd>, FixedFd>;

inline void prep_sendto(io_uring_sqe *sqe, int sockfd, const void *buf,
                        size_t len, int flags, const struct sockaddr *addr,
                        socklen_t addrlen) {
    io_uring_prep_send(sqe, sockfd, buf, len, flags);
    io_uring_prep_send_set_addr(sqe, addr, addrlen);
}

inline void prep_send_fixed(io_uring_sqe *sqe, int sockfd, const void *buf,
                            size_t len, int flags, int buf_index) {
    io_uring_prep_send(sqe, sockfd, buf, len, flags);
    sqe->ioprio |= IORING_RECVSEND_FIXED_BUF;
    sqe->buf_index = buf_index;
}

inline void prep_sendto_fixed(io_uring_sqe *sqe, int sockfd, const void *buf,
                              size_t len, int flags,
                              const struct sockaddr *addr, socklen_t addrlen,
                              int buf_index) {
    prep_sendto(sqe, sockfd, buf, len, flags, addr, addrlen);
    sqe->ioprio |= IORING_RECVSEND_FIXED_BUF;
    sqe->buf_index = buf_index;
}

inline void prep_sendto_zc(io_uring_sqe *sqe, int sockfd, const void *buf,
                           size_t len, int flags, const struct sockaddr *addr,
                           socklen_t addrlen, unsigned zc_flags) {
    io_uring_prep_send_zc(sqe, sockfd, buf, len, flags, zc_flags);
    io_uring_prep_send_set_addr(sqe, addr, addrlen);
}

inline void prep_sendto_zc_fixed(io_uring_sqe *sqe, int sockfd, const void *buf,
                                 size_t len, int flags,
                                 const struct sockaddr *addr, socklen_t addrlen,
                                 unsigned zc_flags, int buf_index) {
    prep_sendto_zc(sqe, sockfd, buf, len, flags, addr, addrlen, zc_flags);
    sqe->ioprio |= IORING_RECVSEND_FIXED_BUF;
    sqe->buf_index = buf_index;
}

} // namespace detail
} // namespace condy