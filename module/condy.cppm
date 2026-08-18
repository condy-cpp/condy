module;

#define IOURINGINLINE inline
#include "condy.hpp"
#undef IOURINGINLINE

export module condy;

export namespace condy {

// version.hpp
using condy::version_major;
using condy::version_minor;

// coro.hpp
using condy::Coro;

// runtime.hpp
using condy::current_runtime;
using condy::Runtime;

// runtime_options.hpp
using condy::RuntimeOptions;

// task.hpp
using condy::co_spawn;
using condy::Task;

// sync_wait.hpp
using condy::default_runtime_options;
using condy::sync_wait;

// buffers.hpp
using condy::buffer;
using condy::ConstBuffer;
using condy::MutableBuffer;

// provided_buffers.hpp
using condy::BufferInfo;
using condy::bundled;
using condy::ProvidedBuffer;
using condy::ProvidedBufferPool;
using condy::ProvidedBufferQueue;

// ring_settings.hpp
using condy::BufferTable;
using condy::FdTable;
using condy::RingSettings;

// channel.hpp
using condy::Channel;

// futex.hpp
using condy::Futex;

// zcrx.hpp
#if CONDY_URING_VERSION_GE(2, 15) // >= 2.15
using condy::ZeroCopyRxArea;
using condy::ZeroCopyRxBuffer;
using condy::ZeroCopyRxBufferPool;
using condy::ZeroCopyRxDMABufArea;
#endif

// cqe_handler.hpp
using condy::NVMePassthruCQEHandler;
using condy::SelectBufferCQEHandler;
using condy::SimpleCQEHandler;
#if CONDY_URING_VERSION_GE(2, 12) // >= 2.12
using condy::TxTimestampCQEHandler;
using condy::TxTimestampResult;
#endif

// helpers.hpp
using condy::file_index_alloc;
using condy::fixed;
using condy::will_push;
using condy::will_spawn;

// sender_operations.hpp
using condy::always_async;
using condy::build_multishot_op_sender;
using condy::build_op_sender;
using condy::build_zero_copy_op_sender;
using condy::drain;
using condy::flag;
using condy::hard_link;
using condy::link;
using condy::parallel;
using condy::when_all;
using condy::when_any;

// awaiter_operations.hpp
using condy::build_multishot_op_awaiter;
using condy::build_op_awaiter;
using condy::build_zero_copy_op_awaiter;

// awaiters.hpp
using condy::HardLinkAwaiter;
using condy::LinkAwaiter;
using condy::ParallelAllAwaiter;
using condy::ParallelAnyAwaiter;
using condy::RangedHardLinkAwaiter;
using condy::RangedLinkAwaiter;
using condy::RangedParallelAllAwaiter;
using condy::RangedParallelAnyAwaiter;
using condy::RangedWhenAllAwaiter;
using condy::RangedWhenAnyAwaiter;
using condy::WhenAllAwaiter;
using condy::WhenAnyAwaiter;

// async_operations.hpp
using condy::async_accept;
using condy::async_accept_direct;
using condy::async_cancel_fd;
using condy::async_close;
using condy::async_connect;
using condy::async_epoll_ctl;
using condy::async_fadvise;
using condy::async_fallocate;
using condy::async_fgetxattr;
using condy::async_files_update;
using condy::async_fsetxattr;
using condy::async_fsync;
using condy::async_getxattr;
using condy::async_link;
using condy::async_link_timeout;
using condy::async_linkat;
using condy::async_madvise;
using condy::async_mkdir;
using condy::async_mkdirat;
using condy::async_multishot_accept;
using condy::async_multishot_accept_direct;
using condy::async_nop;
using condy::async_open;
using condy::async_open_direct;
using condy::async_openat;
using condy::async_openat2;
using condy::async_openat2_direct;
using condy::async_openat_direct;
using condy::async_read;
using condy::async_readv;
using condy::async_recv;
using condy::async_recv_multishot;
using condy::async_recvmsg;
using condy::async_recvmsg_multishot;
using condy::async_rename;
using condy::async_renameat;
using condy::async_send;
using condy::async_send_zc;
using condy::async_sendmsg;
using condy::async_sendmsg_zc;
using condy::async_sendto;
using condy::async_sendto_zc;
using condy::async_setxattr;
using condy::async_shutdown;
using condy::async_socket;
using condy::async_socket_direct;
using condy::async_splice;
using condy::async_statx;
using condy::async_symlink;
using condy::async_symlinkat;
using condy::async_sync_file_range;
using condy::async_tee;
using condy::async_timeout;
using condy::async_unlink;
using condy::async_unlinkat;
using condy::async_uring_cmd;
using condy::async_uring_cmd_multishot;
using condy::async_write;
using condy::async_writev;
#if CONDY_URING_VERSION_GE(2, 4) // >= 2.4
using condy::async_timeout_multishot;
#endif
#if CONDY_URING_VERSION_GE(2, 5) // >= 2.5
using condy::async_cmd_sock;
#endif
#if CONDY_URING_VERSION_GE(2, 6) // >= 2.6
using condy::async_fixed_fd_install;
using condy::async_ftruncate;
using condy::async_futex_wait;
using condy::async_futex_waitv;
using condy::async_futex_wake;
using condy::async_read_multishot;
using condy::async_waitid;
#endif
#if CONDY_URING_VERSION_GE(2, 7) // >= 2.7
using condy::async_bind;
using condy::async_fadvise64;
using condy::async_listen;
using condy::async_madvise64;
#endif
#if CONDY_URING_VERSION_GE(2, 8) // >= 2.8
using condy::async_cmd_discard;
#endif
#if CONDY_URING_VERSION_GE(2, 10) // >= 2.10
using condy::async_epoll_wait;
#endif
#if CONDY_URING_VERSION_GE(2, 12) // >= 2.12
using condy::async_pipe;
using condy::async_pipe_direct;
#endif
#if CONDY_URING_VERSION_GE(2, 13) // >= 2.13
using condy::async_cmd_getsockname;
using condy::async_nop128;
using condy::async_uring_cmd128;
#endif
#if CONDY_URING_VERSION_GE(2, 16) // >= 2.16
using condy::async_cmd_zone_reset_all;
#endif

} // namespace condy

export namespace condy::operators {

// sender_operations.hpp
using condy::operators::operator&&;
using condy::operators::operator||;
using condy::operators::operator>>;

} // namespace condy::operators

export namespace condy::pmr {

// pmr.hpp
using condy::pmr::Coro;
using condy::pmr::Task;

} // namespace condy::pmr
