/**
 * @file MtcTickSource.h
 * @brief Timecode tick source driven by MTC quarter-frame messages.
 *
 * Wraps MtcReceiver's static-member interface behind an OO facade. Owns the
 * MIDI port lifecycle and routes the quarter-frame callback to downstream
 * consumers (e.g., GradientEngine).
 *
 * ## Initialization order
 *
 * The caller MUST follow this sequence:
 *   1. setTickCallback(cb)   — register the tick handler
 *   2. start(midiPort)       — open the MIDI port and begin receiving
 *
 * Calling start() before setTickCallback() is legal; ticks will be silently
 * discarded until a callback is registered (MtcReceiver null-checks it).
 *
 * ## Thread safety
 *
 * - getMtcMs() and isRunning() are safe from any thread at any time.
 * - setTickCallback() and start() are initialization-time methods and MUST
 *   NOT be called concurrently.
 * - The registered callback fires from the RtMidi MIDI callback thread and
 *   MUST be lock-free and non-blocking.
 *
 * ## One-instance constraint (FR-009)
 *
 * MtcReceiver uses process-global static members. Only one MtcTickSource
 * may exist per process at a time. Constructing a second instance produces
 * undefined behavior.
 *
 * @example Typical usage:
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
    ~MtcTickSource() = default;

    // Non-copyable, non-movable (owns a unique hardware resource)
    MtcTickSource(const MtcTickSource&) = delete;
    MtcTickSource& operator=(const MtcTickSource&) = delete;

    /**
     * @brief Register the quarter-frame tick callback.
     *
     * The callback fires on every MTC quarter-frame decode, carrying the
     * current MTC head position in milliseconds. At 25 fps it fires at
     * 200 Hz (8 quarter frames per video frame).
     *
     * Must be called before start(). Registering a callback after start() is
     * undefined behavior. The callback MUST be lock-free and non-blocking —
     * it fires from the RtMidi MIDI callback thread.
     *
     * @param cb  Callable invoked with the current MTC head position (ms).
     *
     * @example
     * @code
     *   src.setTickCallback([](long ms) {
     *       engine.evaluateAt(ms);
     *   });
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
     * @example
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
     * @example
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
     * @example
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
