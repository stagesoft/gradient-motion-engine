/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file StatusEmitRequest.h
 * @brief Status kind enum and status-emit tuple pushed from the tick thread to
 *        the NNG status worker thread.
 *
 * Defined in `libgradient_motion` (`src/signal/`) so that `MotionRegistry`
 * can produce status tuples without depending on any daemon header.
 * `NngBusClient` (daemon side) keeps a `using StatusKind = gme::signal::StatusKind`
 * alias so its existing call sites compile unchanged.
 *
 * ## Allocation note
 *
 * `std::string` members (`fade_id`, `reason`) are **not** on the per-frame
 * steady-state tick path — they are copied only when a motion completes or
 * errors (non-recurring lifecycle events). This is acceptable per
 * Constitution Principle IV.
 *
 * @par Example usage:
 * @code
 *   gme::signal::StatusEmitRequest req;
 *   req.kind    = gme::signal::StatusKind::MotionComplete;
 *   req.fade_id = motion.motion_id;
 *   if (!statusQueue_.push(std::move(req)))
 *       GME_LOG_WARNING("status queue overflow — oldest dropped");
 * @endcode
 */

#pragma once

#include <string>

namespace gme {
namespace signal {

/**
 * @brief Discriminates between a successful motion completion and an error event.
 *
 * - `MotionComplete` — serialised as `data.event: "motion_complete"`.
 * - `MotionError`    — serialised as `data.event: "motion_error"`.
 *
 * Standard reason strings for `MotionError`:
 *  - `"osc_send_failed"` — consecutive OSC failures reached threshold.
 *  - `"osc_address_failed"` — `lo_address_new` returned null.
 *  - `"unknown_curve_type"` — `CurveFactory::createCurve` returned nullopt.
 *  - `"superseded"` — a new motion replaced this one on the same OSC path.
 *  - `"duplicate_motion_id"` — incoming motion reused an already-active id.
 *  - `"parse_error"` — NNG envelope failed validation.
 */
enum class StatusKind {
    MotionComplete, ///< Motion reached t = 1.0 and was removed from the registry.
    MotionError     ///< Motion was removed due to an error (see reason field).
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
    StatusKind  kind    = StatusKind::MotionComplete; ///< Event kind.
    std::string fade_id;  ///< Originating motion identifier (wire field name preserved; rename deferred to wire-v2).
    std::string reason;   ///< Non-empty for MotionError (e.g. "osc_send_failed").
};

} // namespace signal
} // namespace gme
