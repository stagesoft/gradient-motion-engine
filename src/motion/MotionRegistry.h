/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file MotionRegistry.h
 * @brief Owns all active motions and drives per-tick evaluation + transport output.
 *
 * `MotionRegistry` is the evaluation core of `libgradient_motion`. It
 * maintains a map of active `IMotion` instances indexed by `motion_id`
 * (primary) and by composite OSC key `"host:port:path"` (secondary, for
 * supersede detection). All transport knowledge lives in concrete `IMotion`
 * subclasses; the registry only manages lifecycle.
 *
 * ## Thread model
 *
 * All public methods must be called from a **single thread at a time** —
 * either the MTC tick callback thread or the 100 ms fallback drain thread,
 * serialised externally via `NngBusClient::drain_in_progress_`.
 * No internal mutex is required.
 *
 * ## Status emission model
 *
 *  - **Tick thread context** (completion, osc_send_failed): push onto the
 *    SPSC `statusQueue_` reference.
 *  - **Drain thread context** (supersede, duplicate_motion_id,
 *    construction errors from MotionFactory): call `statusDirect_` directly.
 *
 * The caller sets `tickThreadContext_` via `setTickThreadContext` before
 * calling `apply` + `tick`, and clears it after.
 *
 * ## addMotion ordered checks
 *
 *  1. **Duplicate-`motion_id` guard**: if `motions_` already contains
 *     the incoming id, emit `MotionError:"duplicate_motion_id"` for the
 *     incoming id, drop the incoming motion, leave the existing one untouched.
 *  2. **`osc_key` supersede**: look up `m->osc_key` in `osc_index_`; on
 *     hit, emit `MotionError:"superseded"` for the old motion, call
 *     `new_motion->inheritFrom(old_motion)`, erase the old.
 *  3. Insert into both `motions_` and `osc_index_`.
 *
 * @see IMotion
 * @see MotionFactory
 * @see gme::osc::OscSender
 */

#pragma once

#include "motion/IMotion.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"
#include "signal/StatusEmitRequest.h"
#include "time/MtcTickSource.h"

#include <functional>
#include <lo/lo.h>
#include <memory>
#include <string>
#include <unordered_map>

namespace gme {
namespace motion {

/**
 * @brief Concrete registry of active motions with per-tick evaluation.
 *
 * Not designed for subclassing. All data members are `private`. New motion
 * kinds extend `IMotion`, not this class.
 */
class MotionRegistry {
public:
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------

    /**
     * @brief Consecutive transport failures that declare a motion dead.
     *
     * At 200 Hz this equals 25 ms of silence — long enough to survive a
     * momentary GC pause in the target player, short enough to notify the
     * Controller quickly on a real crash.
     */
    static constexpr int kOscFailureThreshold = 5;

    /** @brief Capacity of the SPSC status queue (matches inbound queue). */
    static constexpr std::size_t kStatusQueueCapacity = 64;

    // -----------------------------------------------------------------------
    // Injectable OSC send function type (for test injection via MotionFactory)
    // -----------------------------------------------------------------------

    /**
     * @brief Function type for sending a float OSC message.
     *
     * Stored and forwarded to `MotionFactory::fromCommand` when a `START_FADE`
     * command is applied. Not called directly by the registry.
     */
    using OscSendFn = std::function<int(lo_address, const char*, float)>;

    // -----------------------------------------------------------------------
    // Constructor
    // -----------------------------------------------------------------------

    /**
     * @brief Construct the registry.
     *
     * @param mtcSource     Reference to the `MtcTickSource` used to resolve
     *                      `FadeCommand::start_mtc_ms == -1` (FR-016).
     *                      Must outlive the registry.
     * @param statusQueue   Reference to the SPSC status queue owned by the
     *                      engine. Tick-thread pushes use this path.
     *                      Must outlive the registry.
     * @param statusDirect  Callback for direct status emission from the
     *                      fallback drain thread context.
     * @param oscSend       Optional OSC send override (default: `sendFloat`).
     *                      Forwarded to `MotionFactory` at `START_FADE` time.
     *
     * @par Example:
     * @code
     *   gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64> sq;
     *   MotionRegistry reg(tickSrc, sq,
     *       [&](auto k, auto& id, auto& r){ client.sendStatus(k, id, r); });
     * @endcode
     */
    MotionRegistry(const gme::time::MtcTickSource& mtcSource,
                   gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64>& statusQueue,
                   std::function<void(gme::signal::StatusKind,
                                     const std::string&,
                                     const std::string&)> statusDirect,
                   OscSendFn oscSend = nullptr);

    ~MotionRegistry() = default;

    MotionRegistry(const MotionRegistry&)            = delete;
    MotionRegistry& operator=(const MotionRegistry&) = delete;

    // -----------------------------------------------------------------------
    // Command dispatch
    // -----------------------------------------------------------------------

    /**
     * @brief Dispatch a `FadeCommand` to the appropriate registry operation.
     *
     * Routes `START_FADE` through `MotionFactory::fromCommand` then
     * `addMotion`; `CANCEL_MOTION` to `cancelMotion`; `CANCEL_ALL` to
     * `cancelAll`. `START_CROSSFADE` is logged and dropped (Phase 4).
     *
     * @param cmd  Command drained from the `LockFreeQueue<FadeCommand, 64>`.
     *
     * @throws Never.
     */
    void apply(gme::signal::FadeCommand& cmd);

    // -----------------------------------------------------------------------
    // Mutating operations
    // -----------------------------------------------------------------------

    /**
     * @brief Insert a fully-constructed motion into the registry.
     *
     * Ordered checks:
     *  1. Duplicate-`motion_id` guard (reject incoming, emit
     *     `MotionError:"duplicate_motion_id"`).
     *  2. `osc_key` supersede (remove old, emit `MotionError:"superseded"`,
     *     call `m->inheritFrom(old)`).
     *  3. Insert into `motions_` and `osc_index_`.
     *
     * @param m  Fully-constructed motion. Must be non-null.
     *           Ownership transferred into the registry.
     *
     * @throws Never.
     */
    void addMotion(std::unique_ptr<IMotion> m);

    /**
     * @brief Cancel a specific motion, optionally snapping to its terminal state.
     *
     * @param motion_id    The motion to cancel. If not found, logs a warning
     *                     and returns.
     * @param snap_to_end  If `true`, calls `m->sendSnapToEnd()` before
     *                     removing.
     *
     * @throws Never.
     */
    void cancelMotion(const std::string& motion_id, bool snap_to_end);

    /**
     * @brief Cancel every active motion without calling `sendSnapToEnd` (FR-009).
     *
     * @throws Never.
     */
    void cancelAll();

    // -----------------------------------------------------------------------
    // Tick evaluation (called exclusively from the MTC tick thread)
    // -----------------------------------------------------------------------

    /**
     * @brief Evaluate all active motions at `mtc_ms` and send transport output.
     *
     * For each `IMotion`:
     *  1. Calls `m->evalAndSend(mtc_ms)`.
     *  2. On `result.failed`: increments `m->consecutive_osc_failures`; if ≥
     *     `kOscFailureThreshold`, pushes `MotionError:"osc_send_failed"` and
     *     marks for removal. Otherwise resets the counter to 0.
     *  3. On `result.completed`: marks `m->completed = true`, pushes
     *     `MotionComplete`, marks for removal.
     *
     * After iterating, removes all marked motions from both maps.
     *
     * @param mtc_ms  Current MTC head position in milliseconds.
     *
     * @throws Never.
     */
    void tick(long mtc_ms);

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------

    /** @brief Return the number of currently active motions. */
    std::size_t size() const noexcept { return motions_.size(); }

    /** @brief Called by GradientEngine::onTick before draining + ticking. */
    void setTickThreadContext(bool v) noexcept { tickThreadContext_ = v; }

private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /** Remove a motion by id from both maps (destructor frees transport handle). */
    void removeMotion(const std::string& motion_id);

    /** Emit a status from the tick thread context (uses statusQueue_). */
    void pushStatusFromTick(gme::signal::StatusKind kind,
                            const std::string& motion_id,
                            const std::string& reason);

    /** Emit a status from the drain thread context (calls statusDirect_). */
    void emitStatusDirect(gme::signal::StatusKind kind,
                          const std::string& motion_id,
                          const std::string& reason);

    /** Route status emission to the correct path based on tickThreadContext_. */
    void emitStatus(gme::signal::StatusKind kind,
                    const std::string& motion_id,
                    const std::string& reason);

    // -----------------------------------------------------------------------
    // State
    // -----------------------------------------------------------------------

    const gme::time::MtcTickSource& mtcSource_;

    gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64>& statusQueue_;

    std::function<void(gme::signal::StatusKind,
                       const std::string&,
                       const std::string&)> statusDirect_;

    OscSendFn oscSend_;

    /**
     * @brief Primary index: motion_id → IMotion.
     *
     * Pointer-stable after insertion (`unordered_map` guarantee), required
     * for future crossfade partner raw-pointer pairing.
     */
    std::unordered_map<std::string, std::unique_ptr<IMotion>> motions_;

    /**
     * @brief Secondary index: `"host:port:path"` → motion_id.
     *
     * Used only in `addMotion` and `cancelMotion` for supersede detection.
     * Never touched during `tick()`.
     */
    std::unordered_map<std::string, std::string> osc_index_;

    bool tickThreadContext_ = false;
};

} // namespace motion
} // namespace gme
