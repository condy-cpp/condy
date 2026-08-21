/**
 * @file cqe_handler.hpp
 * @brief Definitions of CQE handlers
 * @details This file defines a series of CQE handlers, which are responsible
 * for processing the completion of asynchronous operations.
 */

#pragma once

#include "condy/concepts.hpp"
#include "condy/detail/context.hpp"
#include "condy/runtime.hpp"
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <utility>

namespace condy {

/**
 * @brief A simple CQE handler that extracts the result from the CQE without any
 * additional processing.
 * @return int32_t The result of the operation, which is the value of `cqe->res`
 * for the corresponding CQE.
 */
struct SimpleCQEHandler {
    int32_t operator()(io_uring_cqe *cqe) noexcept { return cqe->res; }
};

/**
 * @brief A CQE handler that returns the selected buffers based on the result of
 * the CQE.
 * @tparam Br The buffer ring type
 * @return std::pair<int32_t, BufferType> A pair containing the
 * result of the operation (the value of `cqe->res`) and the selected buffer,
 * whose type is determined by the buffer ring.
 */
template <typename Br>
    requires(requires(Br *br, io_uring_cqe *cqe) { br->handle_finish(cqe); })
class SelectBufferCQEHandler {
public:
    SelectBufferCQEHandler(Br *buffers) : buffers_(buffers) {}

    auto operator()(io_uring_cqe *cqe) noexcept {
        return std::make_pair(cqe->res, buffers_->handle_finish(cqe));
    }

private:
    Br *buffers_;
};

/**
 * @brief A CQE handler for NVMe passthrough commands that extracts the status
 * and result from the CQE.
 * @return std::pair<int32_t, uint64_t> A pair containing the status and result
 * of the NVMe command.
 */
struct NVMePassthruCQEHandler {
    std::pair<int32_t, uint64_t> operator()(io_uring_cqe *cqe) noexcept {
        assert(
            detail::Context::current().runtime()->ring_internal().check_cqe32(
                cqe) &&
            "Expected big CQE for NVMe passthrough");
        return {cqe->res, cqe->big_cqe[0]};
    }
};

/**
 * @brief Result for SCSI BSG passthrough commands.
 * @details Contains the raw res2 from the CQE. Use the member functions
 * to extract individual fields.
 */
struct SCSIBsgResult {
    /** @brief Raw res2 from the CQE. */
    uint64_t res2;

    /** @brief Extract SCSI device status byte. */
    uint8_t device_status() noexcept { return res2 & 0xff; }

    /** @brief Extract driver status. */
    uint8_t driver_status() noexcept { return (res2 >> 8) & 0xff; }

    /** @brief Extract host status. */
    uint8_t host_status() noexcept { return (res2 >> 16) & 0xff; }

    /** @brief Extract sense data length. */
    uint8_t sense_len() noexcept { return (res2 >> 24) & 0xff; }

    /** @brief Extract residual transfer length. */
    uint32_t resid_len() noexcept { return res2 >> 32; }
};

/**
 * @brief A CQE handler for SCSI BSG passthrough commands that extracts the
 * SCSI status and result from the CQE.
 * @return std::pair<int32_t, SCSIBsgResult> A pair containing the result of
 * the operation (the value of `cqe->res`) and the raw SCSI status.
 */
struct SCSIBsgPassthruCQEHandler {
    std::pair<int32_t, SCSIBsgResult> operator()(io_uring_cqe *cqe) noexcept {
        assert(
            detail::Context::current().runtime()->ring_internal().check_cqe32(
                cqe) &&
            "Expected big CQE for SCSI BSG passthrough");
        return {cqe->res, SCSIBsgResult{cqe->big_cqe[0]}};
    }
};

#if CONDY_URING_VERSION_GE(2, 12) // >= 2.12
/**
 * @brief Result for TX timestamp operations, containing timestamp information
 * from the socket error queue
 */
struct TxTimestampResult {
    /**
     * @brief The timestamp type, could be SCM_TSTAMP_SND, SCM_TSTAMP_SCHED,
     * SCM_TSTAMP_ACK, etc.
     */
    int tstype; // cqe->flags >> IORING_TIMESTAMP_TYPE_SHIFT

    /**
     * @brief Whether this timestamp is a hardware timestamp.
     */
    bool hwts; // cqe->flags & IORING_CQE_F_TSTAMP_HW

    /**
     * @brief The timestamp value.
     */
    io_timespec ts; // *(io_timespec *)(cqe->big_cqe)
};

/**
 * @brief A CQE handler for TX timestamp operations that extracts timestamp
 * information from the CQE.
 * @return std::pair<int32_t, TxTimestampResult> Result of the TX timestamp
 * operation.
 */
struct TxTimestampCQEHandler {
    std::pair<int32_t, TxTimestampResult>
    operator()(io_uring_cqe *cqe) noexcept {
        assert(
            detail::Context::current().runtime()->ring_internal().check_cqe32(
                cqe) &&
            "Expected big CQE for TX timestamp operations");
        TxTimestampResult result;
        result.tstype =
            static_cast<int>(cqe->flags >> IORING_TIMESTAMP_TYPE_SHIFT);
        result.hwts = cqe->flags & IORING_CQE_F_TSTAMP_HW;
        result.ts = reinterpret_cast<io_timespec &>(cqe->big_cqe);
        return {cqe->res, result};
    }
};
#endif

} // namespace condy