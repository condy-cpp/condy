/**
 * @file provided_buffers.hpp
 * @brief Support for io_uring provided buffers.
 * @details This file provides support for io_uring provided buffers, which can
 * be used as an alternative to regular buffers in asynchronous operations.
 */

#pragma once

#include "condy/concepts.hpp"
#include "condy/condy_uring.hpp"
#include "condy/detail/buffers.hpp"
#include "condy/detail/context.hpp"
#include "condy/detail/ring.hpp"
#include "condy/detail/utils.hpp"
#include "condy/runtime.hpp"
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/types.h>
#include <vector>

namespace condy {

/**
 * @brief Information about buffers consumed from a provided buffer queue.
 * @details This structure contains information about the buffers that have been
 * consumed from a provided buffer queue, including the buffer ID and the number
 * of buffers consumed. If the buffer is partial comsumption, num_buffers will
 * be zero. If multiple buffers are consumed, num_buffers will indicate how many
 * buffers were used, and the buffer ID will correspond to the first buffer
 * used.
 */
struct BufferInfo {
    /**
     * @brief Buffer ID of the first buffer consumed.
     */
    uint16_t bid;
    /**
     * @brief Number of buffers consumed.
     */
    uint16_t num_buffers;
};

namespace detail {

class BundledProvidedBufferQueue {
protected:
    BundledProvidedBufferQueue(Runtime *runtime, uint32_t capacity,
                               unsigned int flags)
        : runtime_(runtime), capacity_(std::bit_ceil(capacity)),
          mask_(io_uring_buf_ring_mask(capacity_)), buf_lens_(capacity_, 0),
          br_flags_(flags) {
        auto &bgid_pool = runtime_->bgid_pool();

        bgid_ = bgid_pool.allocate();
        auto d = detail::defer([&]() { bgid_pool.recycle(bgid_); });

        int err = 0;
        br_ = io_uring_setup_buf_ring(runtime_->ring().ring(), capacity_, bgid_,
                                      br_flags_, &err);
        if (br_ == nullptr) [[unlikely]] {
            throw detail::make_system_error("io_uring_setup_buf_ring", -err);
        }

        d.dismiss();
    }

    ~BundledProvidedBufferQueue() {
        assert(br_ != nullptr);
        [[maybe_unused]] int r = io_uring_free_buf_ring(runtime_->ring().ring(),
                                                        br_, capacity_, bgid_);
        assert(r == 0);
        if (r == 0) {
            runtime_->bgid_pool().recycle(bgid_);
        }
    }

public:
    CONDY_DELETE_COPY_MOVE(BundledProvidedBufferQueue);

public:
    /**
     * @brief Get the current size of the buffer queue
     */
    size_t size() const noexcept { return size_; }

    /**
     * @brief Get the capacity of the buffer queue
     */
    size_t capacity() const noexcept { return capacity_; }

    /**
     * @brief Push a buffer into the provided buffer queue
     * @tparam Buffer Type of the buffer
     * @param buffer The buffer to be pushed
     * @return uint16_t The buffer ID assigned to the pushed buffer.
     * @throws std::logic_error if the capacity of the queue is exceeded
     * @note The returned buffer ID is always sequentially ordered, starting
     * from 0 and incrementing by 1 for each buffer pushed into the queue,
     * wrapping around when reaching the queue's capacity.
     */
    template <BufferLike Buffer> uint16_t push(const Buffer &buffer) {
        if (size_ >= capacity_) [[unlikely]] {
            throw std::logic_error("Capacity exceeded");
        }

        uint16_t bid = br_->tail & mask_;
        io_uring_buf_ring_add(br_, buffer.data(), buffer.size(), bid, mask_, 0);
        buf_lens_[bid] = buffer.size();
        io_uring_buf_ring_advance(br_, 1);
        size_++;

        return bid;
    }

public:
    uint16_t bgid() const noexcept { return bgid_; }

    BufferInfo handle_finish(io_uring_cqe *cqe) noexcept {
        assert(cqe != nullptr);
        int32_t res = cqe->res;
        uint32_t flags = cqe->flags;

        if (!(flags & IORING_CQE_F_BUFFER)) {
            return BufferInfo{0, 0};
        }

        assert(res > 0);

        BufferInfo result = {
            .bid = static_cast<uint16_t>(flags >> IORING_CQE_BUFFER_SHIFT),
            .num_buffers = 0,
        };

#if !IO_URING_CHECK_VERSION(2, 8) // >= 2.8
        if (flags & IORING_CQE_F_BUF_MORE) {
            assert(buf_lens_[result.bid] > static_cast<uint32_t>(res));
            buf_lens_[result.bid] -= res;
            return result;
        }
#endif

        bool is_incr = false;
#if !IO_URING_CHECK_VERSION(2, 8) // >= 2.8
        is_incr = br_flags_ & IOU_PBUF_RING_INC;
#endif

        uint16_t idx = result.bid;
        int64_t bytes = res;
        while (bytes > 0) {
            uint16_t curr_bid = idx & mask_;
            uint32_t buf_len;
            if (is_incr) {
                buf_len = std::min<uint32_t>(bytes, buf_lens_[curr_bid]);
                buf_lens_[curr_bid] -= buf_len;
            } else {
                buf_len = std::exchange(buf_lens_[curr_bid], 0);
            }
            bytes -= buf_len;
            if (buf_lens_[curr_bid] == 0) {
                result.num_buffers++;
            }
            idx++;
        }
        assert(size_ >= result.num_buffers);
        size_ -= result.num_buffers;

        return result;
    }

private:
    Runtime *runtime_;
    io_uring_buf_ring *br_ = nullptr;
    uint32_t size_ = 0;
    uint32_t capacity_;
    int mask_;
    uint16_t bgid_;
    std::vector<uint32_t> buf_lens_;
    unsigned int br_flags_;
};

} // namespace detail

/**
 * @brief Provided buffer queue.
 * @details A provided buffer queue manages a queue of buffers that can be used
 * in asynchronous operations. User is responsible for pushing buffers into the
 * queue.
 * @returns std::pair<int32_t, BufferInfo> When passed to async operations, the
 * return type will be a pair of the operation result and the @ref BufferInfo.
 * @note The lifetime of this queue must not exceed the running period of the
 * associated Runtime. The buffers pushed into the queue must remain valid until
 * they are consumed from the queue.
 */
class ProvidedBufferQueue : public detail::BundledProvidedBufferQueue {
public:
    /**
     * @brief Construct a new ProvidedBufferQueue object in current Runtime.
     * @param capacity Number of buffers the queue can hold.
     * @param flags Optional flags for io_uring buffer ring registration
     * (default: 0).
     */
    ProvidedBufferQueue(uint32_t capacity, unsigned int flags = 0)
        : ProvidedBufferQueue(*detail::Context::current().runtime(), capacity,
                              flags) {}

    /**
     * @brief Construct a new Provided Buffer Queue object with specified
     * Runtime.
     * @param runtime The Runtime to associate with this buffer queue.
     * @param capacity Number of buffers the queue can hold.
     * @param flags Optional flags for io_uring buffer ring registration
     * (default: 0).
     */
    ProvidedBufferQueue(Runtime &runtime, uint32_t capacity,
                        unsigned int flags = 0)
        : BundledProvidedBufferQueue(&runtime, capacity, flags) {}

    BufferInfo handle_finish(io_uring_cqe *cqe) noexcept {
        assert(cqe != nullptr);
        auto result = BundledProvidedBufferQueue::handle_finish(cqe);
        assert(result.num_buffers <= 1);
        return result;
    }
};

namespace detail {
class BundledProvidedBufferPool;
}

/**
 * @brief Provided buffer.
 * @details A provided buffer represents a buffer obtained from a provided
 * buffer pool. It automatically returns the buffer to the pool when it goes
 * out of scope.
 * @note The lifetime of the provided buffer must not exceed the lifetime of the
 * provided buffer pool it is associated with.
 */
using ProvidedBuffer = detail::ManagedBuffer<detail::BundledProvidedBufferPool>;

namespace detail {

class BundledProvidedBufferPool {
protected:
    BundledProvidedBufferPool(Runtime *runtime, void *buffer_data,
                              uint32_t num_buffers, size_t buffer_size,
                              unsigned int flags)
        : runtime_(runtime), buffers_base_(static_cast<char *>(buffer_data)),
          num_buffers_(std::bit_ceil(num_buffers)),
          mask_(io_uring_buf_ring_mask(num_buffers_)),
          buffer_size_(buffer_size), curr_buf_len_(buffer_size),
          br_flags_(flags) {
        auto &bgid_pool = runtime_->bgid_pool();

        bgid_ = bgid_pool.allocate();
        auto d = detail::defer([&]() { bgid_pool.recycle(bgid_); });

        int err = 0;
        br_ = io_uring_setup_buf_ring(runtime_->ring().ring(), num_buffers_,
                                      bgid_, br_flags_, &err);
        if (br_ == nullptr) [[unlikely]] {
            throw detail::make_system_error("io_uring_setup_buf_ring", -err);
        }

        for (size_t bid = 0; bid < num_buffers_; bid++) {
            char *ptr = buffers_base_ + bid * buffer_size_;
            io_uring_buf_ring_add(br_, ptr, buffer_size_, bid, mask_,
                                  static_cast<int>(bid));
        }
        io_uring_buf_ring_advance(br_, static_cast<int>(num_buffers_));

        d.dismiss();
    }

    ~BundledProvidedBufferPool() {
        assert(br_ != nullptr);
        [[maybe_unused]] int r = io_uring_free_buf_ring(
            runtime_->ring().ring(), br_, num_buffers_, bgid_);
        assert(r == 0);
        if (r == 0) {
            runtime_->bgid_pool().recycle(bgid_);
        }
    }

public:
    CONDY_DELETE_COPY_MOVE(BundledProvidedBufferPool);

public:
    /**
     * @brief Get the capacity of the buffer pool
     */
    size_t capacity() const noexcept { return num_buffers_; }

    /**
     * @brief Get the size of each buffer in the pool
     */
    size_t buffer_size() const noexcept { return buffer_size_; }

public:
    uint16_t bgid() const noexcept { return bgid_; }

    std::vector<ProvidedBuffer> handle_finish(io_uring_cqe *cqe) noexcept {
        assert(cqe != nullptr);
        int32_t res = cqe->res;
        uint32_t flags = cqe->flags;
        std::vector<ProvidedBuffer> buffers;

        if (!(flags & IORING_CQE_F_BUFFER)) {
            return buffers;
        }

        assert(res > 0);

        uint16_t bid = flags >> IORING_CQE_BUFFER_SHIFT;

#if !IO_URING_CHECK_VERSION(2, 8) // >= 2.8
        if (flags & IORING_CQE_F_BUF_MORE) {
            char *data = get_buffer_(bid) + (buffer_size_ - curr_buf_len_);
            buffers.emplace_back(data, res, nullptr);
            assert(static_cast<uint32_t>(res) < curr_buf_len_);
            curr_buf_len_ -= res;
            return buffers;
        }
#endif
        assert(bid == curr_io_uring_buf_()->bid);

        bool is_incr = false;
#if !IO_URING_CHECK_VERSION(2, 8) // >= 2.8
        is_incr = br_flags_ & IOU_PBUF_RING_INC;
#endif

        int64_t bytes = res;
        while (bytes > 0) {
            auto *buf_ptr = curr_io_uring_buf_();
            bid = buf_ptr->bid;

            char *data = get_buffer_(bid) + (buffer_size_ - curr_buf_len_);
            uint32_t buf_len;
            if (is_incr) {
                buf_len = std::min<uint32_t>(bytes, curr_buf_len_);
                curr_buf_len_ -= buf_len;
            } else {
                buf_len = std::exchange(curr_buf_len_, 0);
            }
            bytes -= buf_len;
            if (curr_buf_len_ == 0) {
                buffers.emplace_back(data, buf_len, this);
                advance_io_uring_buf_();
                curr_buf_len_ = buffer_size_;
            } else {
                buffers.emplace_back(data, buf_len, nullptr);
            }
        }

        return buffers;
    }

    void add_buffer_back(void *ptr, [[maybe_unused]] size_t size) noexcept {
        assert(size <= buffer_size_);
        assert(ptr >= buffers_base_);
        size_t offset = static_cast<char *>(ptr) - buffers_base_;
        size_t bid = offset / buffer_size_;
        assert(bid < num_buffers_);
        char *buffer_ptr = buffers_base_ + bid * buffer_size_;
        io_uring_buf_ring_add(br_, buffer_ptr, buffer_size_, bid, mask_, 0);
        io_uring_buf_ring_advance(br_, 1);
    }

private:
    char *get_buffer_(uint16_t bid) const noexcept {
        return buffers_base_ + static_cast<size_t>(bid) * buffer_size_;
    }

    io_uring_buf *curr_io_uring_buf_() noexcept {
        return &br_->bufs[br_head_ & mask_];
    }

    void advance_io_uring_buf_() noexcept { br_head_++; }

protected:
    Runtime *runtime_;
    io_uring_buf_ring *br_ = nullptr;
    char *buffers_base_ = nullptr;
    uint32_t num_buffers_;
    int mask_;
    uint32_t buffer_size_;
    uint32_t curr_buf_len_;
    uint16_t bgid_;
    uint16_t br_head_ = 0;
    unsigned int br_flags_;
};

} // namespace detail

/**
 * @brief Provided buffer pool.
 * @details A provided buffer pool manages a pool of buffers that can be used in
 * asynchronous operations. Only receiving operations can obtain buffers from
 * the pool.
 * @returns std::pair<int32_t, ProvidedBuffer> When passed to async operations,
 * the return type will be a pair of the operation result and the @ref
 * ProvidedBuffer.
 * @note The lifetime of this pool must not exceed the running period of the
 * associated Runtime, and the lifetime of any ProvidedBuffer obtained from
 * this pool must not exceed the lifetime of this pool.
 */
class ProvidedBufferPool : public detail::BundledProvidedBufferPool {
public:
    /**
     * @brief Construct a new ProvidedBufferPool object in current Runtime.
     * @param num_buffers Number of buffers to allocate in the pool.
     * @param buffer_size Size of each buffer in bytes.
     * @param flags Optional flags for io_uring buffer registration (default:
     * 0).
     */
    ProvidedBufferPool(uint32_t num_buffers, size_t buffer_size,
                       unsigned int flags = 0)
        : ProvidedBufferPool(*detail::Context::current().runtime(), num_buffers,
                             buffer_size, flags) {}

    /**
     * @brief Construct a new Provided Buffer Pool object with specified
     * Runtime.
     * @param runtime The Runtime to associate with this buffer pool.
     * @param num_buffers Number of buffers to allocate in the pool.
     * @param buffer_size Size of each buffer in bytes.
     * @param flags Optional flags for io_uring buffer registration (default:
     * 0).
     */
    ProvidedBufferPool(Runtime &runtime, uint32_t num_buffers,
                       size_t buffer_size, unsigned int flags = 0)
        : BundledProvidedBufferPool(
              &runtime,
              alloc_buffer_data_(std::bit_ceil(num_buffers) * buffer_size),
              num_buffers, buffer_size, flags),
          external_memory_(false) {}

    /**
     * @brief Construct with externally provided buffer memory.
     * @param buffer_data Pointer to externally allocated buffer memory.
     * @param num_buffers Number of buffers in the pool.
     * @param buffer_size Size of each buffer in bytes.
     * @param flags Optional flags for io_uring buffer registration.
     */
    ProvidedBufferPool(void *buffer_data, uint32_t num_buffers,
                       size_t buffer_size, unsigned int flags = 0)
        : ProvidedBufferPool(*detail::Context::current().runtime(), buffer_data,
                             num_buffers, buffer_size, flags) {}

    /**
     * @brief Construct with externally provided buffer memory and specified
     * Runtime.
     * @param runtime The Runtime to associate with this buffer pool.
     * @param buffer_data Pointer to externally allocated buffer memory.
     * @param num_buffers Number of buffers in the pool.
     * @param buffer_size Size of each buffer in bytes.
     * @param flags Optional flags for io_uring buffer registration.
     */
    ProvidedBufferPool(Runtime &runtime, void *buffer_data,
                       uint32_t num_buffers, size_t buffer_size,
                       unsigned int flags = 0)
        : BundledProvidedBufferPool(&runtime, buffer_data, num_buffers,
                                    buffer_size, flags),
          external_memory_(true) {}

    ~ProvidedBufferPool() {
        if (!external_memory_) {
            munmap(buffers_base_, capacity() * buffer_size());
        }
    }

public:
    ProvidedBuffer handle_finish(io_uring_cqe *cqe) noexcept {
        assert(cqe != nullptr);
        auto buffers = BundledProvidedBufferPool::handle_finish(cqe);
        if (buffers.empty()) {
            return ProvidedBuffer();
        }
        assert(buffers.size() == 1);
        return std::move(buffers[0]);
    }

private:
    static void *alloc_buffer_data_(size_t buf_size) {
        void *buf_data = mmap(nullptr, buf_size, PROT_READ | PROT_WRITE,
                              MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
        if (buf_data == MAP_FAILED) [[unlikely]] {
            throw detail::make_system_error("mmap");
        }
        return buf_data;
    }

    bool external_memory_ = false;
};

/**
 * @brief Get the bundled variant of a provided buffer pool. This will
 * enable buffer bundling feature of io_uring.
 * @param buffer The provided buffer pool.
 * @return auto& The bundled variant of the provided buffer.
 * @note When using bundled provided buffer pool, the return type of async
 * operations will be a vector of @ref ProvidedBuffer instead of a single
 * buffer.
 */
inline auto &bundled(ProvidedBufferPool &buffer) {
    return static_cast<detail::BundledProvidedBufferPool &>(buffer);
}

/**
 * @brief Get the bundled variant of a provided buffer queue. This will
 * enable buffer bundling feature of io_uring.
 * @param buffer The provided buffer queue.
 * @return auto& The bundled variant of the provided buffer.
 */
inline auto &bundled(ProvidedBufferQueue &buffer) {
    return static_cast<detail::BundledProvidedBufferQueue &>(buffer);
}

} // namespace condy