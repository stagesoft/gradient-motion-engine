/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file MtcTickSource.h
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
 * - getMtcMs() and isRunning() are safe from any thread.
 * - setTickCallback() is thread-safe (v2.0.0 adds an internal mutex).
 * - The registered callback fires from the RtMidi MIDI callback thread
 *   and MUST be lock-free and non-blocking.
 *
 * ## One-instance constraint (spec 004 FR-009, carry-forward from spec 003)
 *
 * MtcReceiver uses process-global static members for its callback slot
 * and decoder state. Only one MtcTickSource may exist per process at a
 * time. Constructing a second instance produces undefined behaviour.
 *
 * @par Typical usage
 * @code
 *   gme::time::MtcTickSource src;
 *
 *   src.setTickCallback([](long ms) {
 *       // Evaluate gradient engine at timecode position ms.
 *       // MUST be lock-free — fires from the MIDI thread.
 *   });
 *
 *   auto err = src.start("MTC");
 *   if (err != gme::time::MtcStartError::kOk) {
 *       // Handle: no MIDI ports, or port name not found.
 *   }
 *
 *   // Query from any thread:
 *   long pos     = src.getMtcMs();
 *   bool running = src.isRunning();
 * @endcode
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
 * Thin adapter over MtcReceiver that exposes a callback-based interface for
 * receiving quarter-frame ticks synchronized to incoming MIDI timecode.
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
     * an already-registered callback atomically swaps the stored closure
     * inside mtcreceiver. The callback MUST be lock-free and non-blocking —
     * it fires from the RtMidi MIDI callback thread.
     *
     * @param cb  Callable invoked with the current MTC head position (ms).
     *            Pass an empty std::function to deregister.
     *
     * @par Example
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
     * Calls MtcReceiver::setNetworkMode(true) unconditionally before opening
     * the port so that network MTC sources (e.g., rtpmidid) work without
     * additional configuration (FR-002).
     *
     * Scans available RtMidi input ports for one whose name contains
     * @p midiPort (case-sensitive substring match). The first match is used.
     *
     * @param midiPort  Substring of the MIDI port name to search for.
     *
     * @return MtcStartError::kOk              on success.
     * @return MtcStartError::kNoPortsAvailable if no MIDI ports exist.
     * @return MtcStartError::kPortNotFound     if no port matches.
     *
     * @par Example
     * @code
     *   auto err = src.start("MTC");
     *   if (err != gme::time::MtcStartError::kOk) {
     *       // log and abort
     *   }
     * @endcode
     */
    MtcStartError start(const std::string& midiPort);

    /**
     * @brief Return the current MTC head position in milliseconds.
     *
     * Reads MtcReceiver::mtcHead atomically. Safe from any thread at any
     * time, including before start().
     *
     * @return Current MTC head in ms. Returns 0 if MTC has never started.
     *
     * @par Example
     * @code
     *   long pos = src.getMtcMs();  // 0 before start(), timecode ms after
     * @endcode
     */
    long getMtcMs() const;

    /**
     * @brief Return whether the MTC timecode transport is currently running.
     *
     * Reads MtcReceiver::isTimecodeRunning atomically. Returns false if no
     * quarter-frame messages have arrived within the running timeout (~100 ms).
     *
     * @return true if MTC is actively running, false if stopped or not started.
     *
     * @par Example
     * @code
     *   bool active = src.isRunning();  // true while MTC is playing
     * @endcode
     */
    bool isRunning() const;

private:
    std::unique_ptr<MtcReceiver> receiver_;  ///< Null until start() is called.
};

} // namespace time
} // namespace gme
