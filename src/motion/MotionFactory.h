/**
 * @file MotionFactory.h
 * @brief Factory for constructing `IMotion` instances from `FadeCommand` records.
 *
 * `MotionFactory` is the single place where `CurveFactory::createCurve` and
 * `lo_address_new` are called. By keeping construction out of
 * `MotionRegistry::addMotion`, the registry only ever sees fully-wired
 * motions and cannot fail in construction-specific ways.
 *
 * ## Extension point (Constitution Principle VII)
 *
 * When a new motion type is added, add a new case to
 * `MotionFactory::fromCommand`'s switch statement and a corresponding
 * `make*` helper. No other changes to the registry or tick loop are required.
 *
 * @see IMotion
 * @see FadeMotion
 */

#pragma once

#include "motion/IMotion.h"
#include "signal/FadeCommand.h"
#include "signal/StatusEmitRequest.h"
#include "time/MtcTickSource.h"

#include <lo/lo.h>
#include <functional>
#include <memory>

namespace gme {
namespace motion {

/**
 * @brief Stateless factory — one free function, one context struct.
 */
class MotionFactory {
public:
    /** @brief Function type matching `gme::osc::sendFloat`. */
    using OscSendFn = std::function<int(lo_address, const char*, float)>;

    /**
     * @brief Construction context passed to `fromCommand`.
     *
     * The registry composes this struct (binding the correct status-emission
     * path) and passes it when handling a `START_FADE` command.
     */
    struct Context {
        /** MTC source for resolving `start_mtc_ms == -1` (FR-016). */
        const gme::time::MtcTickSource& mtcSource;

        /** OSC send function forwarded into the new motion at construction. */
        OscSendFn oscSend;

        /**
         * Status callback for construction-phase errors (unknown_curve_type,
         * osc_address_failed). Already routed to the correct emission path
         * (queue vs direct) by the registry before the context is assembled.
         */
        std::function<void(gme::signal::StatusKind,
                           const std::string&,
                           const std::string&)> emitStatus;
    };

    /**
     * @brief Construct an `IMotion` from a parsed `FadeCommand`.
     *
     * Dispatches on `cmd.type`:
     *  - `START_FADE`      → constructs and returns a `FadeMotion`.
     *  - `START_CROSSFADE` → logs and returns `nullptr` (Phase 7).
     *  - others            → returns `nullptr`.
     *
     * On construction failure (`unknown_curve_type`, `osc_address_failed`)
     * emits a `MotionError` via `ctx.emitStatus` and returns `nullptr`.
     * The registry must not call `addMotion` when `nullptr` is returned.
     *
     * @param cmd  Parsed command. `cmd.type` must be one of the above.
     * @param ctx  Construction context (MTC source, send fn, error callback).
     *
     * @return Fully-constructed `IMotion` on success; `nullptr` on failure
     *         or unsupported command type.
     *
     * @throws Never.
     *
     * @par Example:
     * @code
     *   MotionFactory::Context ctx{ mtcSrc, gme::osc::sendFloat, emitFn };
     *   auto m = MotionFactory::fromCommand(cmd, ctx);
     *   if (m) registry.addMotion(std::move(m));
     * @endcode
     */
    static std::unique_ptr<IMotion> fromCommand(const gme::signal::FadeCommand& cmd,
                                                 const Context& ctx);

    MotionFactory() = delete;
};

} // namespace motion
} // namespace gme
