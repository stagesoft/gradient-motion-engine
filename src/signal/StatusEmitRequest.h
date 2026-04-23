/**
 * @file StatusEmitRequest.h
 * @brief Status kind enum and status-emit tuple pushed from the tick thread to
 *        the NNG status worker thread.
 *
 * Defined in `libgradient_motion` (`src/signal/`) so that `FadeRegistry`
 * can produce status tuples without depending on any daemon header.
 * `NngBusClient` (daemon side) keeps a `using StatusKind = gme::signal::StatusKind`
 * alias so its existing call sites compile unchanged.
 *
 * ## Allocation note
 *
 * `std::string` members (`fade_id`, `reason`) are **not** on the per-frame
 * steady-state tick path — they are copied only when a fade completes or
 * errors (non-recurring lifecycle events). This is acceptable per
 * Constitution Principle IV.
 *
 * @par Example usage:
 * @code
 *   gme::signal::StatusEmitRequest req;
 *   req.kind    = gme::signal::StatusKind::FadeComplete;
 *   req.fade_id = fade.fade_id;
 *   if (!statusQueue_.push(std::move(req)))
 *       GME_LOG_WARNING("status queue overflow — oldest dropped");
 * @endcode
 */

#pragma once

#include <string>

namespace gme {
namespace signal {

/**
 * @brief Discriminates between a successful fade completion and an error event.
 *
 * - `FadeComplete` — serialised as `data.event: "fade_complete"`.
 * - `FadeError`    — serialised as `data.event: "fade_error"`.
 */
enum class StatusKind {
    FadeComplete, ///< Fade reached t = 1.0 and was removed from the registry.
    FadeError     ///< Fade was removed due to an error (see reason field).
};

/**
 * @brief Tuple pushed by the tick thread onto the SPSC status worker queue.
 *
 * The tick thread produces these via `LockFreeQueue::push`; the NNG status
 * worker thread in `NngBusClient` pops them and calls `sendStatus`.
 *
 * @see NngBusClient::pushStatus
 * @see NngBusClient::statusWorkerLoop
 */
struct StatusEmitRequest {
    StatusKind  kind    = StatusKind::FadeComplete; ///< Event kind.
    std::string fade_id;                            ///< Originating fade identifier.
    std::string reason;                             ///< Non-empty for FadeError (e.g. "osc_send_failed").
};

} // namespace signal
} // namespace gme
