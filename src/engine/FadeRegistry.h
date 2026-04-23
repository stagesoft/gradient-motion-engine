/**
 * @file FadeRegistry.h
 * @brief Owns all active fades and drives per-tick evaluation + OSC output.
 *
 * `FadeRegistry` is the evaluation core of `libgradient_motion`. It maintains
 * a map of active `ActiveFade` entries indexed by `fade_id` (primary) and by
 * composite OSC key `"host:port:path"` (secondary, for supersede detection).
 *
 * ## Thread model
 *
 * All public methods must be called from a **single thread at a time** — either
 * the MTC tick callback thread or the 100 ms fallback drain thread, serialised
 * externally via `NngBusClient::drain_in_progress_` (research.md Decision 1).
 * No internal mutex is required.
 *
 * ## Status emission model (research.md Decision 1)
 *
 *  - **Tick thread context** (completion, osc_send_failed, unknown_curve_type,
 *    osc_address_failed): push onto the SPSC `statusQueue_` reference. The NNG
 *    status worker thread drains this queue and calls `sendStatus`.
 *  - **Drain thread context** (supersede from `addFade`, parse-error curve):
 *    call `statusDirect_` which routes to `NngBusClient::sendStatus` directly
 *    (thread-safe per Phase 3 research).
 *
 * @see ActiveFade
 * @see gme::osc::OscSender
 */

#pragma once

#include "engine/ActiveFade.h"
#include "signal/FadeCommand.h"
#include "signal/LockFreeQueue.h"
#include "signal/StatusEmitRequest.h"
#include "time/MtcTickSource.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace gme {
namespace engine {

/**
 * @brief Registry of active fades with per-tick evaluation.
 *
 * See file-level documentation for the full threading and status-emission
 * contract.
 */
class FadeRegistry {
public:
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------

    /**
     * @brief Consecutive `lo_send` failures that declare a fade dead.
     *
     * At 200 Hz this equals 25 ms of silence — long enough to survive a
     * momentary GC pause in the target player, short enough to notify the
     * Controller quickly on a real crash (research.md Decision 5).
     */
    static constexpr int kOscFailureThreshold = 5;

    /** @brief Capacity of the SPSC status queue (matches inbound queue). */
    static constexpr std::size_t kStatusQueueCapacity = 64;

    // -----------------------------------------------------------------------
    // Injectable OSC send function (default: gme::osc::sendFloat)
    // -----------------------------------------------------------------------

    /**
     * @brief Function type for sending a float OSC message.
     *
     * Defaulted to `gme::osc::sendFloat`. Injectable in unit tests to simulate
     * OSC send failures without needing a network.
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
     *                      engine. Tick-thread pushes use this path (FR-006b).
     *                      Must outlive the registry.
     * @param statusDirect  Callback for direct status emission from the
     *                      fallback drain thread context. Typically wraps
     *                      `NngBusClient::sendStatus`.
     * @param oscSend       Optional OSC send override (default: `sendFloat`).
     *                      Pass a non-null function to inject failures in tests.
     *
     * @par Example:
     * @code
     *   gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64> sq;
     *   FadeRegistry reg(tickSrc, sq,
     *       [&](auto k, auto& id, auto& r){ client.sendStatus(k, id, r); });
     * @endcode
     */
    FadeRegistry(const gme::time::MtcTickSource& mtcSource,
                 gme::signal::LockFreeQueue<gme::signal::StatusEmitRequest, 64>& statusQueue,
                 std::function<void(gme::signal::StatusKind,
                                   const std::string&,
                                   const std::string&)> statusDirect,
                 OscSendFn oscSend = nullptr);

    ~FadeRegistry();

    FadeRegistry(const FadeRegistry&)            = delete;
    FadeRegistry& operator=(const FadeRegistry&) = delete;

    // -----------------------------------------------------------------------
    // Command dispatch (called after draining the LockFreeQueue<FadeCommand>)
    // -----------------------------------------------------------------------

    /**
     * @brief Dispatch a `FadeCommand` to the appropriate registry method.
     *
     * Handles `START_FADE`, `CANCEL_FADE`, `CANCEL_ALL`. `START_CROSSFADE`
     * commands are logged and dropped (Phase 4 assumption; full semantics
     * deferred to Phase 7).
     *
     * @param cmd  Command drained from `LockFreeQueue<FadeCommand, 64>`.
     *
     * @throws Never.
     */
    void apply(gme::signal::FadeCommand& cmd);

    // -----------------------------------------------------------------------
    // Mutating operations (called from tick/drain thread)
    // -----------------------------------------------------------------------

    /**
     * @brief Register a new fade from a `START_FADE` command.
     *
     * Steps (per `specs/006-fade-registry-tick-loop/contracts/fade-registry-api.md`):
     *  1. `CurveFactory::createCurve` on the tick thread (FR-015). On `nullopt`:
     *     emit `FadeError:"unknown_curve_type"` and return.
     *  2. Resolve `cmd.start_mtc_ms == -1` via `mtcSource_.getMtcMs()` (FR-016).
     *  3. Compute supersede key `"host:port:path"`. If it already exists in
     *     `osc_index_`: remove the old fade without final OSC, emit
     *     `FadeError:"superseded"` for the old fade_id (FR-014).
     *  4. `lo_address_new(host, port)`. On null: emit
     *     `FadeError:"osc_address_failed"` and return.
     *  5. Insert into `fades_` and `osc_index_`.
     *
     * @param cmd  Parsed `START_FADE` command. Must have `type == START_FADE`.
     *
     * @throws Never.
     */
    void addFade(const gme::signal::FadeCommand& cmd);

    /**
     * @brief Cancel a specific fade, optionally snapping to its `end_value`.
     *
     * @param fade_id    The fade to cancel. If not found, a warning is logged
     *                   and the call is a no-op.
     * @param snap_to_end  If `true`, send one final OSC message at `end_value`
     *                     before removing (FR-008). If `false`, no final send
     *                     (FR-008).
     *
     * @throws Never.
     */
    void cancelFade(const std::string& fade_id, bool snap_to_end);

    /**
     * @brief Cancel every active fade without sending final OSC values (FR-009).
     *
     * Typically called on project unload or SIGTERM.
     *
     * @throws Never.
     */
    void cancelAll();

    // -----------------------------------------------------------------------
    // Tick evaluation (called exclusively from the MTC tick thread)
    // -----------------------------------------------------------------------

    /**
     * @brief Evaluate all active fades at `mtc_ms` and send OSC.
     *
     * For each `ActiveFade`:
     *  1. Compute `t = clamp((mtc_ms − start_mtc_ms) / duration_ms, 0, 1)`.
     *     Special case: `duration_ms == 0 → t = 1.0` (immediate completion).
     *  2. Compute `value = start_value + (end_value − start_value) * curve->evaluate(t)`.
     *  3. Call `oscSend_(osc_target, osc_path, value)`.
     *  4. Update `last_sent_value`.
     *  5. On `ret != 0`: increment `consecutive_osc_failures`. If ≥
     *     `kOscFailureThreshold`: push `FadeError:"osc_send_failed"`, mark
     *     for removal.  Otherwise: reset `consecutive_osc_failures = 0`.
     *  6. If `t >= 1.0`: mark `completed = true`, push `FadeComplete`.
     *
     * After iterating: remove all fades marked for removal or completed from
     * both `fades_` and `osc_index_`.
     *
     * @param mtc_ms  Current MTC head position in milliseconds.
     *
     * @throws Never. `oscSend_` is required to be `noexcept`-equivalent.
     */
    void tick(long mtc_ms);

    // -----------------------------------------------------------------------
    // Diagnostics
    // -----------------------------------------------------------------------

    /** @brief Return the number of currently active fades. */
    std::size_t size() const noexcept { return fades_.size(); }

private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /** Compute the OSC supersede key from a fade entry. */
    static std::string oscKey(const std::string& host, int port,
                              const std::string& path);

    /** Remove a fade by ID from both maps and free its lo_address. */
    void removeFade(const std::string& fade_id);

    /**
     * Emit a status event from the **tick thread** context (uses statusQueue_).
     */
    void pushStatusFromTick(gme::signal::StatusKind kind,
                            const std::string& fade_id,
                            const std::string& reason);

    /**
     * Emit a status event from the **drain thread** context (calls statusDirect_).
     */
    void emitStatusDirect(gme::signal::StatusKind kind,
                          const std::string& fade_id,
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
     * @brief Primary index: fade_id → ActiveFade.
     *
     * Pointer-stable after insertion (`unordered_map` guarantee), which is
     * required for Phase 7 `crossfade_partner` raw pointer (research.md
     * Decision 3).
     */
    std::unordered_map<std::string, std::unique_ptr<ActiveFade>> fades_;

    /**
     * @brief Secondary index: `"host:port:path"` → fade_id.
     *
     * Used only in `addFade` and `cancelFade` for supersede detection
     * (FR-014). Never touched during `tick()`.
     */
    std::unordered_map<std::string, std::string> osc_index_;

    /**
     * @brief Set to `true` when `tick()` is running (not the drain path).
     *
     * Used to route status emission: tick context → queue, drain context →
     * direct call.  Set by callers (GradientEngine::onTick) before calling
     * `apply` + `tick`, cleared after.
     */
    bool tickThreadContext_ = false;

public:
    /** @brief Called by GradientEngine::onTick before draining + ticking. */
    void setTickThreadContext(bool v) noexcept { tickThreadContext_ = v; }
};

} // namespace engine
} // namespace gme
