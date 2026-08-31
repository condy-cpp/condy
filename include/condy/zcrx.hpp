/**
 * @file zcrx.hpp
 * @brief Support for io_uring zero-copy receive (zcrx) buffers.
 * @details This file provides support for the io_uring zcrx feature, which
 * allows data to be received directly into user-space buffers without copying.
 * It defines the ZeroCopyRxBufferPool class for registering a buffer pool with
 * a network interface, and the ZeroCopyRxBuffer type for buffers obtained from
 * the pool.
 */

#pragma once

#include "condy/detail/buffers.hpp"
#include "condy/detail/context.hpp"
#include "condy/detail/ring.hpp"
#include "condy/detail/utils.hpp"
#include "condy/runtime.hpp"
#include <bit>
#include <sys/mman.h>

namespace condy {

#if CONDY_URING_VERSION_GE(2, 15) // >= 2.15

class ZeroCopyRxBufferPool;

/**
 * @brief Buffer from a ZeroCopyRxBufferPool.
 * @details This buffer type is used for buffers obtained from a
 * ZeroCopyRxBufferPool. It automatically returns the buffer to the pool when it
 * is out of scope.
 * @note The lifetime of the buffer must not exceed the lifetime of the
 * ZeroCopyRxBufferPool it is associated with.
 */
class ZeroCopyRxBuffer : public detail::ManagedBuffer<ZeroCopyRxBufferPool> {
public:
    using Base = detail::ManagedBuffer<ZeroCopyRxBufferPool>;
    using Base::Base;
};

/**
 * @brief Area for zero-copy receive buffers.
 */
struct ZeroCopyRxArea {
    /** @brief Starting address of the buffer area. If null, the pool will
     *         allocate memory internally. */
    void *addr = nullptr;
    /** @brief Size of the buffer area in bytes. */
    size_t size;
};

/**
 * @brief Area for zero-copy receive buffers using DMA-BUF.
 */
struct ZeroCopyRxDMABufArea {
    /** @brief File descriptor of the DMA-BUF to use. */
    int dmabuf_fd;
    /** @brief Offset into the DMA-BUF where the buffer area starts. */
    size_t offset;
    /** @brief Size of the buffer area within the DMA-BUF, in bytes. */
    size_t size;
};

/**
 * @brief Buffer pool for zero-copy receive buffers.
 * @details This buffer pool utilizes the io_uring zcrx feature to provide
 * zero-copy receive buffers. It can be used to receive data directly into
 * user-space buffers without copying, which can improve performance for
 * high-throughput network applications.
 * @returns std::pair<int32_t, ZeroCopyRxBuffer> When passed to async
 * operations, the return type will be a pair of the operation result and the
 * @ref ZeroCopyRxBuffer.
 * @note The lifetime of this pool must not exceed the running period of the
 * associated Runtime, and the lifetime of any ZeroCopyRxBuffer obtained from
 * this pool must not exceed the lifetime of this pool.
 */
class ZeroCopyRxBufferPool {
public:
    /**
     * @brief Construct a new Zero Copy Rx Buffer Pool object
     * @param if_idx Network interface index to register the buffer pool with.
     * @param if_rxq Receive queue index to register the buffer pool with.
     * @param rq_entries Number of receive queue entries.
     * @param area Area for zero-copy receive buffers.
     */
    ZeroCopyRxBufferPool(uint32_t if_idx, uint32_t if_rxq, uint32_t rq_entries,
                         const ZeroCopyRxArea &area)
        : ZeroCopyRxBufferPool(*detail::Context::current().runtime(), if_idx,
                               if_rxq, rq_entries, area) {}

    /**
     * @brief Construct a new Zero Copy Rx Buffer Pool object
     * @param runtime The runtime to register the buffer pool with.
     * @param if_idx Network interface index to register the buffer pool with.
     * @param if_rxq Receive queue index to register the buffer pool with.
     * @param rq_entries Number of receive queue entries.
     * @param area Area for zero-copy receive buffers.
     */
    ZeroCopyRxBufferPool(Runtime &runtime, uint32_t if_idx, uint32_t if_rxq,
                         uint32_t rq_entries, const ZeroCopyRxArea &area)
        : ZeroCopyRxBufferPool(runtime.ring_internal(), if_idx, if_rxq,
                               rq_entries, area, 0) {}

    // Device-less constructor, DO NOT use this in production code if you don't
    // know what you are doing.
    ZeroCopyRxBufferPool(Runtime &runtime, uint32_t rq_entries,
                         const ZeroCopyRxArea &area)
        : ZeroCopyRxBufferPool(runtime.ring_internal(), 0, 0, rq_entries, area,
                               ZCRX_REG_NODEV) {}

    /**
     * @brief Construct a new Zero Copy Rx Buffer Pool object
     * @param if_idx Network interface index to register the buffer pool with.
     * @param if_rxq Receive queue index to register the buffer pool with.
     * @param rq_entries Number of receive queue entries.
     * @param area Area for zero-copy receive buffers using DMA-BUF.
     */
    ZeroCopyRxBufferPool(uint32_t if_idx, uint32_t if_rxq, uint32_t rq_entries,
                         const ZeroCopyRxDMABufArea &area)
        : ZeroCopyRxBufferPool(*detail::Context::current().runtime(), if_idx,
                               if_rxq, rq_entries, area) {}

    /**
     * @brief Construct a new Zero Copy Rx Buffer Pool object
     * @param runtime The runtime to register the buffer pool with.
     * @param if_idx Network interface index to register the buffer pool with.
     * @param if_rxq Receive queue index to register the buffer pool with.
     * @param rq_entries Number of receive queue entries.
     * @param area Area for zero-copy receive buffers using DMA-BUF.
     */
    ZeroCopyRxBufferPool(Runtime &runtime, uint32_t if_idx, uint32_t if_rxq,
                         uint32_t rq_entries, const ZeroCopyRxDMABufArea &area)
        : ring_(&runtime.ring_internal()), flags_(0) {
        bool ok = false;
        auto d = detail::defer([&]() {
            if (!ok) {
                cleanup_();
            }
        });

        area_size_ = 0;
        area_ptr_ = nullptr;

        io_uring_zcrx_area_reg area_reg = {};
        area_reg.addr = area.offset;
        area_reg.len = area.size;
        area_reg.flags = IORING_ZCRX_AREA_DMABUF;

        register_ifq_(if_idx, if_rxq, rq_entries, area_reg,
                      sysconf(_SC_PAGESIZE));
        ok = true;
    }

    ~ZeroCopyRxBufferPool() { cleanup_(); }

    CONDY_DELETE_COPY_MOVE(ZeroCopyRxBufferPool);

private:
    ZeroCopyRxBufferPool(detail::Ring &ring, uint32_t if_idx, uint32_t if_rxq,
                         uint32_t rq_entries, const ZeroCopyRxArea &area,
                         uint32_t flags)
        : ring_(&ring), flags_(flags) {
        bool ok = false;
        auto d = detail::defer([&]() {
            if (!ok) {
                cleanup_();
            }
        });

        const size_t page_size = sysconf(_SC_PAGESIZE);

        if (area.addr == nullptr) {
            area_size_ = detail::align_up(area.size, page_size);
            area_ptr_ = mmap(nullptr, area_size_, PROT_READ | PROT_WRITE,
                             MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
            if (area_ptr_ == MAP_FAILED) {
                throw detail::make_system_error("mmap");
            }

            io_uring_zcrx_area_reg area_reg = {};
            area_reg.addr = reinterpret_cast<uint64_t>(area_ptr_);
            area_reg.len = area_size_;
            area_reg.flags = 0;

            register_ifq_(if_idx, if_rxq, rq_entries, area_reg, page_size);
        } else {
            // Not owned, so we don't track the size for unmapping
            area_size_ = 0;
            area_ptr_ = area.addr;

            io_uring_zcrx_area_reg area_reg = {};
            area_reg.addr = reinterpret_cast<uint64_t>(area_ptr_);
            area_reg.len = area.size;
            area_reg.flags = 0;

            register_ifq_(if_idx, if_rxq, rq_entries, area_reg, page_size);
        }

        ok = true;
    }

public:
    uint32_t zcrx_id() const noexcept { return zcrx_id_; }

    ZeroCopyRxBuffer handle_finish(io_uring_cqe *cqe) noexcept {
        if (cqe->res < 0) {
            return ZeroCopyRxBuffer();
        }
        io_uring_zcrx_cqe *rcqe =
            reinterpret_cast<io_uring_zcrx_cqe *>(cqe->big_cqe);
        void *data = static_cast<char *>(area_ptr_) +
                     (rcqe->off & ~IORING_ZCRX_AREA_MASK);
        size_t size = static_cast<size_t>(cqe->res);
        return ZeroCopyRxBuffer(data, size, this);
    }

    void add_buffer_back(void *ptr, size_t size) noexcept {
        rq_enqueue_(ptr, size);
        maybe_flush_rq_();
    }

private:
    void cleanup_() noexcept {
        [[maybe_unused]] int r;
        if (area_ptr_ != nullptr && area_size_ > 0) {
            r = munmap(area_ptr_, area_size_);
            assert(r == 0);
        }
        if (rq_ring_.ring_ptr != nullptr) {
            r = munmap(rq_ring_.ring_ptr, ring_size_);
            assert(r == 0);
        }
        // TODO: Unregister ifq. For now there's no way to unregister ifq, so we
        // just leak the registration.
    }

    void register_ifq_(uint32_t if_idx, uint32_t if_rxq, uint32_t rq_entries,
                       io_uring_zcrx_area_reg &area_reg, size_t page_size) {
        rq_entries = std::bit_ceil(rq_entries);
        io_uring_region_desc region_reg = {};
        ring_size_ = get_refill_ring_size_(rq_entries, page_size);
        region_reg.user_addr = 0;
        region_reg.size = ring_size_;
        region_reg.flags = 0;

        io_uring_zcrx_ifq_reg reg = {};
        reg.if_idx = if_idx;
        reg.if_rxq = if_rxq;
        reg.rq_entries = rq_entries;
        reg.area_ptr = reinterpret_cast<uint64_t>(&area_reg);
        reg.region_ptr = reinterpret_cast<uint64_t>(&region_reg);
        reg.flags = flags_;

        int r = io_uring_register_ifq(ring_->ring(), &reg);
        if (r != 0) {
            throw detail::make_system_error("io_uring_register_ifq", -r);
        }
        // TODO: Unregister ifq if any exception. For now there's no way to
        // unregister ifq.

        void *ring_ptr = mmap(nullptr, ring_size_, PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_POPULATE, ring_->ring()->ring_fd,
                              static_cast<off_t>(region_reg.mmap_offset));
        if (ring_ptr == MAP_FAILED) {
            throw detail::make_system_error("mmap");
        }
        rq_ring_.khead = (unsigned int *)((char *)ring_ptr + reg.offsets.head);
        rq_ring_.ktail = (unsigned int *)((char *)ring_ptr + reg.offsets.tail);
        rq_ring_.rqes =
            (struct io_uring_zcrx_rqe *)((char *)ring_ptr + reg.offsets.rqes);
        rq_ring_.rq_tail = 0;
        rq_ring_.ring_entries = reg.rq_entries;
        rq_ring_.ring_ptr = ring_ptr;

        zcrx_id_ = reg.zcrx_id;
        area_token_ = area_reg.rq_area_token;
    }

    static size_t get_refill_ring_size_(uint32_t rq_entries,
                                        size_t page_size) noexcept {
        size_t ring_size = rq_entries * sizeof(io_uring_zcrx_rqe);
        ring_size += page_size;
        ring_size = detail::align_up(ring_size, page_size);
        return ring_size;
    }

    size_t rq_nr_queued_() const noexcept {
        return rq_ring_.rq_tail - io_uring_smp_load_acquire(rq_ring_.khead);
    }

    void rq_enqueue_(void *ptr, size_t size) noexcept {
        assert(rq_nr_queued_() < rq_ring_.ring_entries);
        io_uring_zcrx_rqe *rqe;
        unsigned rq_mask = rq_ring_.ring_entries - 1;
        rqe = &rq_ring_.rqes[rq_ring_.rq_tail & rq_mask];
        rqe->off = (static_cast<char *>(ptr) - static_cast<char *>(area_ptr_)) |
                   area_token_;
        rqe->len = static_cast<uint32_t>(size);
        io_uring_smp_store_release(rq_ring_.ktail, ++rq_ring_.rq_tail);
    }

    void flush_rq_() noexcept {
        zcrx_ctrl ctrl = {};
        ctrl.zcrx_id = zcrx_id_;
        ctrl.op = ZCRX_CTRL_FLUSH_RQ;
        [[maybe_unused]] int r =
            io_uring_register_zcrx_ctrl(ring_->ring(), &ctrl);
        assert(r == 0);
    }

    void maybe_flush_rq_() noexcept {
        if (rq_nr_queued_() >= rq_ring_.ring_entries ||
            (flags_ & ZCRX_REG_NODEV)) {
            flush_rq_();
        }
    }

private:
    detail::Ring *ring_;
    size_t area_size_ = 0;
    void *area_ptr_ = nullptr;
    size_t ring_size_ = 0;
    io_uring_zcrx_rq rq_ring_ = {};
    uint32_t zcrx_id_;
    uint64_t area_token_;
    uint32_t flags_;
};

#endif

} // namespace condy