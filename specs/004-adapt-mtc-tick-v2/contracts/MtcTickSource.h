/**
 * @file MtcTickSource.h (CONTRACT — specs/004-adapt-mtc-tick-v2)
 * @brief Public C++ contract for gme::time::MtcTickSource after adaptation
 *        to mtcreceiver v2.0.0.
 *
 * This file is the design-time contract only. The canonical implementation
 * lives at src/time/MtcTickSource.h and must stay consistent with this
 * contract. Changes to either side MUST be reconciled in the same PR that
 * introduces them.
 *
 * ## Changes vs spec 003 (v1)
 *
 *   - setTickCallback() no longer assigns MtcReceiver::onQuarterFrame
 *     directly. It adapts the consumer's `void(long)` to the v2.0.0
 *     `void(long, bool)` signature by discarding isCompleteFrame, and
 *     registers via MtcReceiver::setTickCallback().
 *   - setTickCallback(empty/null) deregisters via setTickCallback({}).
 *   - The destructor is no longer `= default`. It explicitly calls
 *     MtcReceiver::setTickCallback({}) to guarantee no-call-after-dtor.
 *   - Docstrings corrected: QF rate at 25 fps is 100 Hz (4 × 25), not
 *     200 Hz as stated in spec 003.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "mtcreceiver/mtcreceiver.h"

namespace gme {
namespace time {

/**
 * @brief Error codes returned by MtcTickSource::start().
 *
 * Explicit error type instead of exceptions, per the project constitution:
 * "Exceptions MUST NOT cross library boundaries."
 */
enum class MtcStartError {
    kOk,                ///< Port opened successfully, MTC reception active.
    kNoPortsAvailable,  ///< No MIDI ports detected on the system.
    kPortNotFound       ///< No port name matched the requested substring.
};

/**
 * @brief Timecode tick source driven by MTC quarter-frame messages.
 *
 * Thin adapter over mtcreceiver v2.0.0. Exposes a `void(long)` callback
 * interface while internally translating to v2.0.0's `void(long, bool)`
 * signature and ignoring the `isCompleteFrame` flag.
 *
 * ## Lifecycle contract
 *
 *   1. setTickCallback(cb)   — register the tick handler (or {} to clear)
 *   2. start(midiPort)       — open the MIDI port and begin receiving
 *   3. ~MtcTickSource()      — deregister; blocks until any in-flight
 *                              invocation returns (v2.0.0 guarantee).
 *
 * ## Thread safety
 *
 *   - getMtcMs() and isRunning() are safe from any thread.
 *   - setTickCallback() is thread-safe (v2.0.0 adds an internal mutex).
 *     Prior undefined-behaviour pattern of assigning a public
 *     std::function from a different thread is eliminated.
 *   - The registered callback fires from the RtMidi MIDI callback thread
 *     and MUST be lock-free and non-blocking.
 *
 * ## One-instance constraint (spec 004 FR-009, carry-forward from spec 003)
 *
 * MtcReceiver uses process-global static members for its callback slot
 * and decoder state. Only one MtcTickSource may exist per process at a
 * time. Constructing a second instance produces undefined behaviour.
 */
class MtcTickSource {
public:
    MtcTickSource() = default;

    /**
     * @brief Deregister the callback (via MtcReceiver::setTickCallback({}))
     *        and release the MIDI port.
     *
     * Blocks until any in-flight callback invocation returns. After this
     * destructor returns, no consumer callback may be invoked — this is
     * the "no call after dtor" guarantee required by FR-004 / SC-004.
     */
    ~MtcTickSource();

    // Non-copyable, non-movable (owns a unique hardware resource)
    MtcTickSource(const MtcTickSource&) = delete;
    MtcTickSource& operator=(const MtcTickSource&) = delete;

    /**
     * @brief Register (or deregister) the quarter-frame tick callback.
     *
     * The callback fires once per MTC quarter frame (at the QF rate —
     * 100 Hz at 25 fps). The `isCompleteFrame` flag introduced by
     * mtcreceiver v2.0.0 is ignored by this adapter; consumers see only
     * the current MTC head position in milliseconds.
     *
     * Thread-safe: may be called at any time, from any thread. Replacing
     * an already-registered callback atomically swaps the stored
     * closure inside mtcreceiver.
     *
     * @param cb  Callable invoked with the current MTC head position (ms).
     *            Pass an empty std::function to deregister.
     *
     * @example
     * @code
     *   src.setTickCallback([](long ms) { engine.evaluateAt(ms); });
     *   // ... later, to stop receiving:
     *   src.setTickCallback({});
     * @endcode
     */
    void setTickCallback(std::function<void(long)> cb);

    /**
     * @brief Open the MIDI port and begin receiving MTC quarter frames.
     *
     * Calls MtcReceiver::setNetworkMode(true) unconditionally before
     * opening the port so that network MTC sources (e.g., rtpmidid) work
     * without additional configuration.
     *
     * Scans available RtMidi input ports for one whose name contains
     * @p midiPort (case-sensitive substring match). The first match is
     * used.
     *
     * @param midiPort  Substring of the MIDI port name to search for.
     *
     * @return MtcStartError::kOk              on success.
     * @return MtcStartError::kNoPortsAvailable if no MIDI ports exist.
     * @return MtcStartError::kPortNotFound     if no port matches.
     */
    MtcStartError start(const std::string& midiPort);

    /**
     * @brief Return the current MTC head position in milliseconds.
     *
     * Reads MtcReceiver::mtcHead atomically. Safe from any thread.
     *
     * @return Current MTC head in ms. Returns 0 if MTC has never started.
     */
    long getMtcMs() const;

    /**
     * @brief Return whether the MTC timecode transport is currently running.
     *
     * Reads MtcReceiver::isTimecodeRunning atomically.
     *
     * @return true if MTC is actively running, false otherwise.
     */
    bool isRunning() const;

private:
    std::unique_ptr<MtcReceiver> receiver_;  ///< Null until start() is called.
};

} // namespace time
} // namespace gme
