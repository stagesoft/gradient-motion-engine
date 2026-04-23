/**
 * @file FadeMotion.h
 * @brief Concrete scalar-fade motion: curve + linear lerp + single-float OSC send.
 *
 * `FadeMotion` is the Phase-4 concrete `IMotion` subclass. It owns:
 *  - A pre-resampled `gme::gradient::Curve` (LUT, 256 samples).
 *  - Scalar `start_value`, `end_value`, `last_sent_value`.
 *  - A pre-built `lo_address` transport handle.
 *
 * ## Evaluation
 *
 * `evalAndSend(mtc_ms)` computes:
 * ```
 * t = clamp((mtc_ms − start_mtc_ms) / duration_ms, 0, 1)
 * value = start_value + (end_value − start_value) * curve.evaluate(t)
 * oscSend_(osc_target, osc_path, value)
 * ```
 *
 * ## Supersede inheritance
 *
 * `inheritFrom(prior)` dynamic-casts to `const FadeMotion*`; on success,
 * copies `prior.last_sent_value` into `this->start_value` and
 * `this->last_sent_value` so the new fade begins at the last OSC position
 * of the outgoing fade without a jump. On type mismatch (e.g. a future
 * `VectorMotion<3>` superseding a `FadeMotion`), the call is a no-op.
 *
 * ## Ownership
 *
 * The destructor calls `lo_address_free(osc_target_)`. `MotionFactory`
 * constructs and hands over both the curve and the address handle.
 *
 * @see IMotion
 * @see MotionFactory
 */

#pragma once

#include "motion/IMotion.h"
#include "gradient/Curve.h"

#include <lo/lo.h>
#include <functional>
#include <memory>
#include <string>

namespace gme {
namespace motion {

/**
 * @brief Scalar fade motion: one OSC float path interpolated over a curve.
 *
 * Constructed exclusively by `MotionFactory::fromCommand`. Do not construct
 * directly outside of `MotionFactory` or tests.
 */
class FadeMotion final : public IMotion {
public:
    /** @brief Function type matching `gme::osc::sendFloat`. */
    using OscSendFn = std::function<int(lo_address, const char*, float)>;

    /**
     * @brief Construct a fully-wired fade motion.
     *
     * @param motion_id     Registry-unique id (from `FadeCommand::fade_id`).
     * @param osc_key       Composite supersede key `"host:port:path"`.
     * @param start_mtc_ms  Absolute MTC start time. Must be ≥ 0.
     * @param duration_ms   Fade duration in ms. 0 → completes on first tick.
     * @param start_value   OSC value at t=0.
     * @param end_value     OSC value at t=1.
     * @param curve         Pre-resampled curve; must be non-null.
     * @param osc_target    Pre-built liblo address; this object takes ownership.
     * @param osc_path      OSC destination path (e.g. `"/volmaster"`).
     * @param osc_host      OSC host string (stored for diagnostics).
     * @param osc_port      UDP port (stored for diagnostics).
     * @param oscSend       Transport send function. Defaults to `gme::osc::sendFloat`.
     *
     * @par Example:
     * @code
     *   // Normally created by MotionFactory::fromCommand, not directly.
     *   lo_address addr = gme::osc::makeAddress("127.0.0.1", 9000);
     *   auto m = std::make_unique<FadeMotion>(
     *       "m1", "127.0.0.1:9000:/gain",
     *       0, 1000.0f, 0.0f, 1.0f,
     *       CurveFactory::createCurve("linear", {}).value(),
     *       addr, "/gain", "127.0.0.1", 9000);
     * @endcode
     */
    FadeMotion(std::string motion_id,
               std::string osc_key,
               long  start_mtc_ms,
               float duration_ms,
               float start_value,
               float end_value,
               std::unique_ptr<gme::gradient::Curve> curve,
               lo_address osc_target,
               std::string osc_path,
               std::string osc_host,
               int   osc_port,
               OscSendFn oscSend);

    /**
     * @brief Destructor. Calls `lo_address_free(osc_target_)`.
     */
    ~FadeMotion() override;

    FadeMotion(const FadeMotion&)            = delete;
    FadeMotion& operator=(const FadeMotion&) = delete;

    // -----------------------------------------------------------------------
    // IMotion interface
    // -----------------------------------------------------------------------

    /**
     * @brief Evaluate scalar lerp and send OSC float.
     *
     * Computes `t`, evaluates the curve, interpolates `start_value`→`end_value`,
     * calls `oscSend_`, and returns `EvalResult`. Does not update
     * `consecutive_osc_failures` — that is managed by `MotionRegistry::tick`.
     *
     * @param mtc_ms  Current MTC head position in milliseconds.
     * @return        `EvalResult { completed=(t>=1.0), failed=(ret!=0) }`.
     *
     * @throws Never.
     */
    EvalResult evalAndSend(long mtc_ms) override;

    /**
     * @brief Send one final OSC message at `end_value`.
     *
     * Called by `MotionRegistry::cancelMotion(snap_to_end=true)`.
     *
     * @throws Never.
     */
    void sendSnapToEnd() override;

    /**
     * @brief Inherit last-sent position from the outgoing fade on the same path.
     *
     * On `dynamic_cast` success: copies `prior.last_sent_value` into
     * `this->start_value` and `this->last_sent_value`.
     * On type mismatch: no-op.
     *
     * @param prior  Outgoing motion being superseded.
     *
     * @throws Never.
     */
    void inheritFrom(const IMotion& prior) override;

    // -----------------------------------------------------------------------
    // Scalar-fade-specific state (public for test access and inheritFrom)
    // -----------------------------------------------------------------------

    float start_value     = 0.0f; ///< OSC value at t=0 (may be updated by inheritFrom).
    float end_value       = 0.0f; ///< OSC value at t=1.
    float last_sent_value = 0.0f; ///< Most recent value sent; updated every tick.

    std::string osc_path; ///< OSC destination path, e.g. `"/volmaster"`.
    std::string osc_host; ///< Host string, stored for diagnostics.
    int         osc_port = 0; ///< UDP port, stored for diagnostics.

private:
    std::unique_ptr<gme::gradient::Curve> curve_;
    lo_address osc_target_ = nullptr;
    OscSendFn  oscSend_;
};

} // namespace motion
} // namespace gme
