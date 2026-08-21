/**
 * @file async_operations.hpp
 * @brief Definitions of asynchronous operations.
 * @details This file defines a series of asynchronous operations, which are
 * wrappers around liburing operations. Generally, each async_* function
 * corresponds to a io_uring_prep_* function in liburing
 */

#pragma once

#include "condy/awaiter_operations.hpp"
#include "condy/concepts.hpp"
#include "condy/condy_uring.hpp"
#include "condy/cqe_handler.hpp"
#include "condy/detail/helpers.hpp"

namespace condy {
namespace detail {

template <typename T> T &&unwrap_fixed(T &&t) noexcept {
    return std::forward<T>(t);
}

inline int unwrap_fixed(FixedFd fd) noexcept { return fd.value; }

template <typename Func, typename... Args>
auto make_op_awaiter(Func &&func, Args &&...args) {
    auto prep_func = [func = std::forward<Func>(func),
                      ... args =
                          unwrap_fixed(std::forward<Args>(args))](Ring *ring) {
        auto *sqe = ring->get_sqe();
        func(sqe, args...);
        return sqe;
    };
    return build_op_awaiter(std::move(prep_func), SimpleCQEHandler{});
}

#if CONDY_URING_VERSION_GE(2, 13) // >= 2.13
template <typename Func, typename... Args>
auto make_op_awaiter128(Func &&func, Args &&...args) {
    auto prep_func = [func = std::forward<Func>(func),
                      ... args =
                          unwrap_fixed(std::forward<Args>(args))](Ring *ring) {
        auto *sqe = ring->get_sqe128();
        if (!sqe) {
            panic_on("SQE128 not enabled in the ring");
        }
        func(sqe, args...);
        return sqe;
    };
    return build_op_awaiter(std::move(prep_func), SimpleCQEHandler{});
}
#endif

template <typename MultiShotFunc, typename Func, typename... Args>
auto make_multishot_op_awaiter(MultiShotFunc &&multishot_func, Func &&func,
                               Args &&...args) {
    auto prep_func = [func = std::forward<Func>(func),
                      ... args =
                          unwrap_fixed(std::forward<Args>(args))](Ring *ring) {
        auto *sqe = ring->get_sqe();
        func(sqe, args...);
        return sqe;
    };
    return build_multishot_op_awaiter(
        std::move(prep_func), SimpleCQEHandler{},
        std::forward<MultiShotFunc>(multishot_func));
}

template <BufferRingLike Br, typename Func, typename... Args>
auto make_select_buffer_op_awaiter(Br *buffers, Func &&func, Args &&...args) {
    auto prep_func = [bgid = buffers->bgid(), func = std::forward<Func>(func),
                      ... args =
                          unwrap_fixed(std::forward<Args>(args))](Ring *ring) {
        auto *sqe = ring->get_sqe();
        func(sqe, args...);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = bgid;
        return sqe;
    };
    return build_op_awaiter(std::move(prep_func),
                            SelectBufferCQEHandler<Br>(buffers));
}

template <typename MultiShotFunc, BufferRingLike Br, typename Func,
          typename... Args>
auto make_multishot_select_buffer_op_awaiter(MultiShotFunc &&multishot_func,
                                             Br *buffers, Func &&func,
                                             Args &&...args) {
    auto prep_func = [bgid = buffers->bgid(), func = std::forward<Func>(func),
                      ... args =
                          unwrap_fixed(std::forward<Args>(args))](Ring *ring) {
        auto *sqe = ring->get_sqe();
        func(sqe, args...);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = bgid;
        return sqe;
    };
    return build_multishot_op_awaiter(
        std::move(prep_func), SelectBufferCQEHandler<Br>(buffers),
        std::forward<MultiShotFunc>(multishot_func));
}

#if CONDY_URING_VERSION_GE(2, 7) // >= 2.7
template <BufferRingLike Br, typename Func, typename... Args>
auto make_bundle_select_buffer_op_awaiter(Br *buffers, Func &&func,
                                          Args &&...args) {
    auto prep_func = [bgid = buffers->bgid(), func = std::forward<Func>(func),
                      ... args =
                          unwrap_fixed(std::forward<Args>(args))](Ring *ring) {
        auto *sqe = ring->get_sqe();
        func(sqe, args...);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = bgid;
        sqe->ioprio |= IORING_RECVSEND_BUNDLE;
        return sqe;
    };
    return build_op_awaiter(std::move(prep_func),
                            SelectBufferCQEHandler<Br>(buffers));
}
#endif

#if CONDY_URING_VERSION_GE(2, 7) // >= 2.7
template <typename MultiShotFunc, BufferRingLike Br, typename Func,
          typename... Args>
auto make_multishot_bundle_select_buffer_op_awaiter(
    MultiShotFunc &&multishot_func, Br *buffers, Func &&func, Args &&...args) {
    auto prep_func = [bgid = buffers->bgid(), func = std::forward<Func>(func),
                      ... args =
                          unwrap_fixed(std::forward<Args>(args))](Ring *ring) {
        auto *sqe = ring->get_sqe();
        func(sqe, args...);
        sqe->flags |= IOSQE_BUFFER_SELECT;
        sqe->buf_group = bgid;
        sqe->ioprio |= IORING_RECVSEND_BUNDLE;
        return sqe;
    };
    return build_multishot_op_awaiter(
        std::move(prep_func), SelectBufferCQEHandler<Br>(buffers),
        std::forward<MultiShotFunc>(multishot_func));
}
#endif

template <typename FreeFunc, typename Func, typename... Args>
auto make_zero_copy_op_awaiter(FreeFunc &&free_func, Func &&func,
                               Args &&...args) {
    auto prep_func = [func = std::forward<Func>(func),
                      ... args =
                          unwrap_fixed(std::forward<Args>(args))](Ring *ring) {
        auto *sqe = ring->get_sqe();
        func(sqe, args...);
        return sqe;
    };
    return build_zero_copy_op_awaiter(std::move(prep_func), SimpleCQEHandler{},
                                      std::forward<FreeFunc>(free_func));
}

template <typename Awaiter>
auto maybe_flag_fixed_fd(Awaiter &&op, const FixedFd &) {
    return flag<IOSQE_FIXED_FILE>(std::forward<Awaiter>(op));
}

template <typename Awaiter> auto maybe_flag_fixed_fd(Awaiter &&op, int) {
    return std::forward<Awaiter>(op);
}

template <typename Fd>
constexpr bool is_fixed_fd_v = std::is_same_v<std::remove_cvref_t<Fd>, FixedFd>;

inline void prep_send_fixed(io_uring_sqe *sqe, int sockfd, const void *buf,
                            size_t len, int flags, int buf_index) noexcept {
    io_uring_prep_send(sqe, sockfd, buf, len, flags);
    sqe->ioprio |= IORING_RECVSEND_FIXED_BUF;
    sqe->buf_index = buf_index;
}

inline void prep_recv_fixed(io_uring_sqe *sqe, int sockfd, void *buf,
                            size_t len, int flags, int buf_index) noexcept {
    io_uring_prep_recv(sqe, sockfd, buf, len, flags);
    sqe->ioprio |= IORING_RECVSEND_FIXED_BUF;
    sqe->buf_index = buf_index;
}

inline void prep_sendto(io_uring_sqe *sqe, int sockfd, const void *buf,
                        size_t len, int flags, const struct sockaddr *addr,
                        socklen_t addrlen) noexcept {
    io_uring_prep_send(sqe, sockfd, buf, len, flags);
    io_uring_prep_send_set_addr(sqe, addr, addrlen);
}

inline void prep_sendto_fixed(io_uring_sqe *sqe, int sockfd, const void *buf,
                              size_t len, int flags,
                              const struct sockaddr *addr, socklen_t addrlen,
                              int buf_index) noexcept {
    prep_sendto(sqe, sockfd, buf, len, flags, addr, addrlen);
    sqe->ioprio |= IORING_RECVSEND_FIXED_BUF;
    sqe->buf_index = buf_index;
}

inline void prep_sendto_zc(io_uring_sqe *sqe, int sockfd, const void *buf,
                           size_t len, int flags, const struct sockaddr *addr,
                           socklen_t addrlen, unsigned zc_flags) noexcept {
    io_uring_prep_send_zc(sqe, sockfd, buf, len, flags, zc_flags);
    io_uring_prep_send_set_addr(sqe, addr, addrlen);
}

inline void prep_sendto_zc_fixed(io_uring_sqe *sqe, int sockfd, const void *buf,
                                 size_t len, int flags,
                                 const struct sockaddr *addr, socklen_t addrlen,
                                 unsigned zc_flags, int buf_index) noexcept {
    prep_sendto_zc(sqe, sockfd, buf, len, flags, addr, addrlen, zc_flags);
    sqe->ioprio |= IORING_RECVSEND_FIXED_BUF;
    sqe->buf_index = buf_index;
}

#if CONDY_URING_VERSION_GE(2, 15) // >= 2.15
inline void prep_recv_zc_multishot(io_uring_sqe *sqe, int fd,
                                   uint32_t zcrx_id) {
    io_uring_prep_rw(IORING_OP_RECV_ZC, sqe, fd, nullptr, 0, 0);
    sqe->ioprio |= IORING_RECV_MULTISHOT;
    sqe->zcrx_ifq_idx = zcrx_id;
}
#endif

} // namespace detail
} // namespace condy