/**
 * @file EvalResult.h
 * @brief POD result returned by IMotion::evalAndSend.
 *
 * The registry uses `completed` and `failed` to schedule removal and
 * status emission. `failure_reason` is a static-storage `const char*`
 * so that the hot path incurs zero heap allocation.
 */

#pragma once

namespace gme {
namespace motion {

/**
 * @brief Result of one `IMotion::evalAndSend` call.
 *
 * @param completed       `true` when `t` reached 1.0 on this tick.
 * @param failed          `true` when this tick's transport send failed.
 *                        `MotionRegistry` tracks consecutive failures and
 *                        removes the motion when `kOscFailureThreshold` is
 *                        reached.
 * @param failure_reason  Static-storage reason used as the `MotionError`
 *                        reason string. Non-null only when `failed == true`.
 */
struct EvalResult {
    bool        completed      = false;
    bool        failed         = false;
    const char* failure_reason = nullptr;
};

} // namespace motion
} // namespace gme
