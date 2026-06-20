/**
 * @file ring_settings.hpp
 * @brief io_uring settings management classes.
 */

#pragma once

#include "condy/condy_uring.hpp"
#include "condy/detail/utils.hpp"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstring>

namespace condy {

/**
 * @brief File descriptor table for io_uring.
 * @details This class makes an abstraction over the io_uring file registration
 * interface.
 */
class FdTable {
public:
    FdTable(io_uring &ring) : ring_(ring) {}

    CONDY_DELETE_COPY_MOVE(FdTable);

public:
    /**
     * @brief Initialize the file descriptor table with the given capacity
     * @param capacity The number of file descriptors to allocate in the table
     * @return int Returns 0 on success or a negative error code on failure
     */
    int init(size_t capacity) noexcept {
        return io_uring_register_files_sparse(&ring_, capacity);
    }

    /**
     * @brief Initialize the file descriptor table with the given array of file
     * descriptors
     * @param fds Pointer to the array of file descriptors to register
     * @param nr_fds Number of file descriptors in the array
     * @return int Returns 0 on success or a negative error code on failure
     */
    int init(const int *fds, unsigned nr_fds) {
        return io_uring_register_files(&ring_, fds, nr_fds);
    }

    /**
     * @brief Destroy the file descriptor table
     * @return int Returns 0 on success or a negative error code on failure
     */
    int destroy() noexcept { return io_uring_unregister_files(&ring_); }

    /**
     * @brief Update the file descriptor table starting from the given index
     * @param index_base The starting index to update
     * @param fds Pointer to the array of file descriptors
     * @param nr_fds Number of file descriptors to update
     * @return int Return number of file descriptors updated on success or a
     * negative error code on failure
     */
    int update(unsigned index_base, const int *fds, unsigned nr_fds) noexcept {
        return io_uring_register_files_update(&ring_, index_base, fds, nr_fds);
    }

    /**
     * @brief Set the file allocation range for the fd table
     * @param offset The starting offset of the file allocation range
     * @param size The size of the file allocation range
     * @return int Returns 0 on success or a negative error code on failure
     */
    int set_file_alloc_range(unsigned offset, unsigned size) noexcept {
        return io_uring_register_file_alloc_range(&ring_, offset, size);
    }

private:
    io_uring &ring_;
};

/**
 * @brief Buffer table for io_uring.
 * @details This class makes an abstraction over the io_uring buffer
 * registration interface.
 */
class BufferTable {
public:
    BufferTable(io_uring &ring) : ring_(ring) {}

    CONDY_DELETE_COPY_MOVE(BufferTable);

public:
    /**
     * @brief Initialize the buffer table with the given capacity
     * @param capacity The number of buffers to allocate in the table
     * @return int Returns 0 on success or a negative error code on failure
     */
    int init(size_t capacity) noexcept {
        return io_uring_register_buffers_sparse(&ring_, capacity);
    }

    /**
     * @brief Initialize the buffer table with the given array of iovec
     * structures
     * @param vecs Pointer to the array of iovec structures representing buffers
     * @param nr_vecs Number of buffers in the array
     * @return int Returns 0 on success or a negative error code on failure
     */
    int init(const iovec *vecs, unsigned nr_vecs) {
        return io_uring_register_buffers(&ring_, vecs, nr_vecs);
    }

    /**
     * @brief Destroy the buffer table
     * @return int Returns 0 on success or a negative error code on failure
     */
    int destroy() noexcept { return io_uring_unregister_buffers(&ring_); }

    /**
     * @brief Update the buffer table starting from the given index
     * @param index_base The starting index to update
     * @param vecs Pointer to the array of iovec structures representing buffers
     * @param nr_vecs Number of buffers to update
     * @return int Returns number of buffers updated on success or a negative
     * error code on failure
     */
    int update(unsigned index_base, const iovec *vecs,
               unsigned nr_vecs) noexcept {
        return io_uring_register_buffers_update_tag(&ring_, index_base, vecs,
                                                    nullptr, nr_vecs);
    }

#if !IO_URING_CHECK_VERSION(2, 10) // >= 2.10

    /**
     * @brief Clone buffers from another BufferTable into this one
     * @param src The source BufferTable to clone from
     * @param dst_off The starting offset in the destination buffer table
     * @param src_off The starting offset in the source buffer table
     * @param nr The number of buffers to clone
     * @return int Returns 0 on success or a negative error code on failure
     */
    int clone_buffers(BufferTable &src, unsigned int dst_off = 0,
                      unsigned int src_off = 0, unsigned int nr = 0) noexcept {
        auto *src_ring = &src.ring_;
        auto *dst_ring = &ring_;
        return __io_uring_clone_buffers_offset(dst_ring, src_ring, dst_off,
                                               src_off, nr,
                                               IORING_REGISTER_DST_REPLACE);
    }
#endif

private:
    io_uring &ring_;
};

/**
 * @brief Settings manager for io_uring.
 * @details This class provides an interface to manage various runtime settings
 * for an io_uring instance, including NAPI, clock, and other features.
 */
class RingSettings {
public:
    RingSettings(io_uring &ring) : ring_(ring) {}

    ~RingSettings() {
        if (probe_) {
            io_uring_free_probe(probe_);
            probe_ = nullptr;
        }
    }

    CONDY_DELETE_COPY_MOVE(RingSettings);

public:
    /**
     * @brief Apply I/O worker queue affinity settings.
     * @details See io_uring_register_iowq_aff for more details.
     * @param cpusz Number of CPUs in the affinity mask.
     * @param mask Pointer to the CPU affinity mask.
     */
    int apply_iowq_aff(size_t cpusz, const cpu_set_t *mask) noexcept {
        return io_uring_register_iowq_aff(&ring_, cpusz, mask);
    }
    /**
     * @brief Remove I/O worker queue affinity settings.
     * @return int Returns 0 on success or a negative error code on failure
     */
    int remove_iowq_aff() noexcept {
        return io_uring_unregister_iowq_aff(&ring_);
    }

    /**
     * @brief Set the maximum number of I/O workers.
     * @details See io_uring_register_iowq_max_workers for more details.
     * @param values Pointer to an array with 2 elements representing the
     * max_workers
     */
    int set_iowq_max_workers(unsigned int *values) noexcept {
        return io_uring_register_iowq_max_workers(&ring_, values);
    }

    /**
     * @brief Get the io_uring probe for the ring.
     * @return io_uring_probe* Pointer to the io_uring probe structure. User
     * shall not free the returned pointer.
     */
    io_uring_probe *get_probe() noexcept {
        if (probe_) {
            return probe_;
        }
        probe_ = io_uring_get_probe_ring(&ring_);
        return probe_;
    }

    /**
     * @brief Get the supported features of the ring.
     * @return uint32_t Supported features bitmask.
     */
    uint32_t get_features() const noexcept { return ring_.features; }

#if !IO_URING_CHECK_VERSION(2, 6) // >= 2.6
    /**
     * @brief Apply NAPI settings to the io_uring instance.
     * @details See io_uring_register_napi for more details.
     * @param napi Pointer to the io_uring_napi structure.
     */
    int apply_napi(io_uring_napi *napi) noexcept {
        return io_uring_register_napi(&ring_, napi);
    }
    /**
     * @brief Remove NAPI settings from the io_uring instance.
     * @param napi Pointer to the io_uring_napi structure. Can be nullptr.
     */
    int remove_napi(io_uring_napi *napi = nullptr) noexcept {
        return io_uring_unregister_napi(&ring_, napi);
    }
#endif

#if !IO_URING_CHECK_VERSION(2, 8) // >= 2.8
    /**
     * @brief Set the clock registration for the io_uring instance.
     * @details See io_uring_register_clock for more details.
     * @param clock_reg Pointer to the io_uring_clock_register structure.
     */
    int set_clock(io_uring_clock_register *clock_reg) noexcept {
        return io_uring_register_clock(&ring_, clock_reg);
    }
#endif

#if !IO_URING_CHECK_VERSION(2, 9) // >= 2.9
    /**
     * @brief Resize the rings of the io_uring instance.
     * @details See io_uring_resize_rings for more details.
     * @deprecated Unsafe during CQE processing; use RuntimeOptions instead.
     * @param params Pointer to the io_uring_params structure.
     */
    [[deprecated("unsafe during CQE processing, use RuntimeOptions")]]
    int set_rings_size(io_uring_params *params) noexcept {
        return io_uring_resize_rings(&ring_, params);
    }
#endif

#if !IO_URING_CHECK_VERSION(2, 10) // >= 2.10
    /**
     * @brief Enable or disable iowait for the io_uring instance.
     * @details See io_uring_set_iowait for more details.
     * @param enable_iowait Boolean flag to enable or disable iowait mode.
     */
    int set_iowait(bool enable_iowait) noexcept {
        return io_uring_set_iowait(&ring_, enable_iowait);
    }
#endif

private:
    io_uring &ring_;
    io_uring_probe *probe_ = nullptr;
};

} // namespace condy