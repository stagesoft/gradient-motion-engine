/**
 * @file ActiveFade.h
 * @brief Data record for a single running fade instance.
 *
 * `ActiveFade` is an internal record owned exclusively by `FadeRegistry`.
 * All fields are set once at `FadeRegistry::addFade` time and thereafter
 * **read** (not written) during `tick()` evaluation except for the mutable
 * fields listed below.
 *
 * ## Mutable fields (updated during tick)
 *  - `last_sent_value` — updated every tick with the value sent via OSC.
 *  - `completed`       — set to `true` when `t >= 1.0`.
 *  - `consecutive_osc_failures` — incremented on non-zero `lo_send` return,
 *    reset to 0 on success.
 *
 * ## Lifetime
 *
 * ```
 * REGISTERED (addFade) → TICKING (each MTC QF) → COMPLETED (t≥1.0) → REMOVED
 *                     ↘ CANCELLED (cancelFade/cancelAll)              ↗ REMOVED
 *                     ↘ ERROR (osc_send_failed, unknown_curve_type)   ↗ REMOVED
 *                     ↘ SUPERSEDED (new fade on same OSC path)        ↗ REMOVED
 * ```
 *
 * ## Invariants
 *
 *  - `start_mtc_ms` is always ≥ 0 after `addFade` (the −1 sentinel is resolved
 *    by `FadeRegistry::addFade` before the record is constructed).
 *  - `osc_target` is non-null while the record is held in the registry;
 *    `FadeRegistry` calls `lo_address_free(osc_target)` when removing the entry.
 *  - `curve` is non-null while the record is held in the registry.
 *
 * @see FadeRegistry
 */

#pragma once

#include "gradient/Curve.h"

#include <lo/lo.h>
#include <memory>
#include <string>

namespace gme {
namespace engine {

/**
 * @brief One active, currently-running fade instance.
 *
 * Plain data struct. Only `FadeRegistry` constructs and destroys instances.
 * Do not create `ActiveFade` objects directly outside of `FadeRegistry`.
 */
struct ActiveFade {
    // -----------------------------------------------------------------------
    // Identity
    // -----------------------------------------------------------------------

    /** @brief Controller-assigned unique fade identifier. */
    std::string fade_id;

    // -----------------------------------------------------------------------
    // Curve evaluation
    // -----------------------------------------------------------------------

    /**
     * @brief Pre-resampled curve (ResampledCurve LUT, 256 samples).
     *
     * Built by `CurveFactory::createCurve` inside `FadeRegistry::addFade` on
     * the tick thread (FR-015). Non-null while in registry.
     */
    std::unique_ptr<gme::gradient::Curve> curve;

    // -----------------------------------------------------------------------
    // OSC target (pre-built at registration — FR-011)
    // -----------------------------------------------------------------------

    /**
     * @brief Pre-built liblo address handle.
     *
     * Created via `gme::osc::makeAddress(osc_host, osc_port)` at `addFade`
     * time. Freed by `FadeRegistry` when the entry is removed.
     * Non-null while in registry.
     */
    lo_address osc_target = nullptr;

    /** @brief OSC destination path, e.g. `"/volmaster"`. */
    std::string osc_path;

    /**
     * @brief OSC target host (stored for supersede-key computation and
     *        `lo_address` reconstruction if ever needed).
     */
    std::string osc_host;

    /** @brief UDP port of the target player. */
    int osc_port = 0;

    // -----------------------------------------------------------------------
    // Fade parameters (immutable after addFade)
    // -----------------------------------------------------------------------

    float start_value  = 0.0f; ///< Gain at t=0. Range [0.0, 1.0].
    float end_value    = 0.0f; ///< Gain at t=1. Range [0.0, 1.0].

    /**
     * @brief Absolute MTC start time in ms.
     *
     * The −1 sentinel from `FadeCommand` is resolved to
     * `MtcTickSource::getMtcMs()` at `addFade` time. Never −1 in a live entry.
     */
    long  start_mtc_ms = 0;

    float duration_ms  = 0.0f; ///< Total fade duration in ms. 0 → completes on first tick.

    // -----------------------------------------------------------------------
    // Mutable tick-state fields
    // -----------------------------------------------------------------------

    /**
     * @brief Most recent value sent via OSC.
     *
     * Initialised to `start_value` at `addFade`. Updated every tick.
     * Used for cancel-with-hold semantics and crash recovery.
     */
    float last_sent_value = 0.0f;

    /**
     * @brief Set to `true` when `t >= 1.0` during a tick.
     *
     * After `tick()` returns, `FadeRegistry` removes all completed entries.
     */
    bool completed = false;

    /**
     * @brief Count of consecutive non-zero `lo_send` return codes.
     *
     * Reset to 0 on any successful send. When it reaches
     * `FadeRegistry::kOscFailureThreshold` the fade is declared dead and
     * removed with a `FadeError:"osc_send_failed"` status.
     */
    int consecutive_osc_failures = 0;

    // -----------------------------------------------------------------------
    // Phase-7 crossfade hook (null in Phase 4)
    // -----------------------------------------------------------------------

    /**
     * @brief Optional crossfade partner pointer (always `nullptr` in Phase 4).
     *
     * Reserved for Phase 7 crossfade pairing. Using a raw pointer is safe
     * because both entries are stored in the same `FadeRegistry::fades_` map,
     * and `std::unordered_map` guarantees pointer stability after insertion
     * (see research.md Decision 3).
     */
    ActiveFade* crossfade_partner = nullptr;
};

} // namespace engine
} // namespace gme
