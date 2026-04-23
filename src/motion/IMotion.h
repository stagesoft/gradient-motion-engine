/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file IMotion.h
 * @brief Abstract base for all motion types in `gme::motion`.
 *
 * `IMotion` defines the common lifecycle contract shared by every concrete
 * motion type (`FadeMotion`, future `VectorMotion<N>`, crossfade types, etc.).
 * The registry (`MotionRegistry`) interacts with all motions exclusively
 * through this interface.
 *
 * ## Lifecycle fields (public)
 *
 * Common identity and lifecycle state is exposed as public fields so that
 * `MotionRegistry` can read them without virtual calls in the hot path.
 * Per-type data (curve, payload arrays, transport handles) MUST live in the
 * derived type, never here.
 *
 * ## Virtual methods
 *
 * Three pure-virtual methods express the per-type behaviour the registry
 * needs to drive:
 *  - `evalAndSend` — called once per active motion per tick.
 *  - `sendSnapToEnd` — called only by `cancelMotion(snap_to_end=true)`.
 *  - `inheritFrom` — called during supersede to transfer state from the
 *    outgoing motion to the incoming one.
 *
 * ## Extension rule (Constitution Principle VII)
 *
 * New motion types MUST subclass `IMotion` and register via
 * `MotionFactory`. No changes to `MotionRegistry` are needed.
 *
 * @see MotionRegistry
 * @see EvalResult
 */

#pragma once

#include "motion/EvalResult.h"

#include <string>

namespace gme {
namespace motion {

/**
 * @brief Abstract base for all motion instances.
 *
 * All public fields are set once at construction time by the concrete type's
 * constructor (via `MotionFactory`). `consecutive_osc_failures` and
 * `completed` are the only fields updated after construction, and only by
 * `MotionRegistry::tick`.
 */
class IMotion {
public:
    // -----------------------------------------------------------------------
    // Common lifecycle fields (set at construction, read by registry)
    // -----------------------------------------------------------------------

    /** @brief Registry-unique motion identifier. Maps to wire field `fade_id`. */
    std::string motion_id;

    /**
     * @brief Composite OSC supersede key: `"host:port:path"`.
     *
     * At most one active motion per `osc_key` at any time. A new motion on
     * the same key supersedes the old one (see `MotionRegistry::addMotion`).
     */
    std::string osc_key;

    /** @brief Absolute MTC start time in milliseconds. Never -1 in a live entry. */
    long start_mtc_ms = 0;

    /** @brief Total motion duration in milliseconds. 0 → completes on first tick. */
    float duration_ms = 0.0f;

    /**
     * @brief Set to `true` when `t >= 1.0` during a tick.
     *
     * After `tick()` returns, the registry removes all completed entries.
     */
    bool completed = false;

    /**
     * @brief Count of consecutive non-zero transport send return codes.
     *
     * Reset to 0 on any successful send. Managed by `MotionRegistry::tick`
     * based on `EvalResult::failed`. When it reaches
     * `MotionRegistry::kOscFailureThreshold` the motion is declared dead.
     */
    int consecutive_osc_failures = 0;

    // -----------------------------------------------------------------------
    // Virtual interface
    // -----------------------------------------------------------------------

    /**
     * @brief Virtual destructor — ensures correct destruction of derived types.
     */
    virtual ~IMotion() = default;

    /**
     * @brief Evaluate the motion at `mtc_ms` and send the result via transport.
     *
     * Called once per active motion per tick from `MotionRegistry::tick`.
     * MUST NOT block, allocate, or throw. Transport send failure MUST be
     * reported via `EvalResult::failed` rather than by throwing.
     *
     * @param mtc_ms  Current MTC head position in milliseconds.
     * @return        `EvalResult` describing completion and failure state.
     *
     * @throws Never.
     */
    virtual EvalResult evalAndSend(long mtc_ms) = 0;

    /**
     * @brief Send one final transport message at the motion's terminal state.
     *
     * Called only by `MotionRegistry::cancelMotion(snap_to_end=true)`.
     * After this call the motion is removed regardless of the send result.
     *
     * @throws Never.
     */
    virtual void sendSnapToEnd() = 0;

    /**
     * @brief Adopt state from the motion being superseded on the same `osc_key`.
     *
     * Called by `MotionRegistry::addMotion` when a new motion supersedes an
     * existing one. `FadeMotion` copies `prior.last_sent_value` into
     * `this->start_value` and `this->last_sent_value` to avoid jumps in the
     * OSC stream. Implementations for types that have no inheritable state
     * MAY be a no-op.
     *
     * @param prior  The outgoing motion being replaced. Use `dynamic_cast` to
     *               access type-specific fields; on mismatch the call is a
     *               no-op.
     *
     * @throws Never.
     */
    virtual void inheritFrom(const IMotion& prior) = 0;

    IMotion(const IMotion&)            = delete;
    IMotion& operator=(const IMotion&) = delete;

protected:
    IMotion() = default;
};

} // namespace motion
} // namespace gme
