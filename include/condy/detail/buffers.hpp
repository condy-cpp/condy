/**
 * @file buffers.hpp
 * @brief Basic buffer types and conversion utilities.
 * @details This file defines basic buffer types and conversion functions.
 * Buffer types are primarily used in asynchronous operations.
 */

#pragma once

#include "condy/detail/utils.hpp"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <utility>

namespace condy {
namespace detail {

template <typename BufferPool> struct ManagedBuffer {
public:
    using CondyBuffer = void;

    ManagedBuffer() = default;
    ManagedBuffer(void *data, size_t size, BufferPool *pool)
        : data_(data), size_(size), pool_(pool) {}
    ManagedBuffer(ManagedBuffer &&other) noexcept
        : data_(std::exchange(other.data_, nullptr)),
          size_(std::exchange(other.size_, 0)),
          pool_(std::exchange(other.pool_, nullptr)) {}
    ManagedBuffer &operator=(ManagedBuffer &&other) noexcept {
        if (this != &other) {
            reset();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
            pool_ = std::exchange(other.pool_, nullptr);
        }
        return *this;
    }

    ~ManagedBuffer() { reset(); }

    CONDY_DELETE_COPY(ManagedBuffer);

public:
    /**
     * @brief Get the data pointer of the buffer
     */
    void *data() const noexcept { return data_; }

    /** *
     * @brief Get the size of the buffer
     */
    size_t size() const noexcept { return size_; }

    /**
     * @brief Reset the buffer, returning it to the pool if owned
     */
    void reset() noexcept {
        if (pool_ != nullptr) {
            pool_->add_buffer_back(data_, size_);
        }
        data_ = nullptr;
        size_ = 0;
        pool_ = nullptr;
    }

    /**
     * @brief Check if the buffer owns a buffer from a pool.
     */
    bool owns_buffer() const noexcept { return pool_ != nullptr; }

private:
    void *data_ = nullptr;
    size_t size_ = 0;
    BufferPool *pool_ = nullptr;
};

} // namespace detail
} // namespace condy