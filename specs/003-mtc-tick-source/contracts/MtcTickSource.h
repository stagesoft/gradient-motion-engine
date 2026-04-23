/**
 * @file MtcTickSource.h
 * @brief Interface contract for gme::time::MtcTickSource.
 *
 * This file documents the public interface of MtcTickSource as a contract
 * specification. It is NOT the implementation header — the implementation
 * lives at src/time/MtcTickSource.h.
 *
 * @note One instance per process. MtcReceiver uses process-global static
 *       members. Constructing two MtcTickSource instances in the same
 *       process produces undefined behavior.
 *
 * @example Typical initialization and usage:
 * @code
 *   gme::time::MtcTickSource src;
 *
 *   src.setTickCallback([](long ms) {
 *       // Evaluate fade curve at timecode position `ms`.
 *       // MUST be lock-free — fires from the MIDI thread.
 *   });
 *
 *   auto err = src.start("MTC");
 *   if (err != gme::time::MtcStartError::kOk) {
 *       // Handle error: log and abort startup.
 *   }
 *
 *   // Query position from any thread:
 *   long pos = src.getMtcMs();
 *   bool running = src.isRunning();
 * @endcode
 */

#pragma once

#include <functional>
#include <string>

namespace gme {
namespace time {

/**
 * @brief Error codes returned by MtcTickSource::start().
 *
 * Used instead of exceptions to comply with the project constitution
 * (Performance & Safety Standards: "Exceptions MUST NOT cross library
 * boundaries").
 */
enum class MtcStartError {
    kOk,                ///< Port opened successfully, MTC reception active.
    kNoPortsAvailable,  ///< No MIDI ports detected on the system.
    kPortNotFound       ///< No port name matched the requested substring.
};

/**
 * @brief Timecode tick source driven by MTC quarter-frame messages.
 *
 * MtcTickSource wraps the MtcReceiver MIDI timecode decoder and exposes
 * a callback-based interface for downstream consumers (e.g., GradientEngine)
 * to receive quarter-frame ticks synchronized to incoming MIDI timecode.
 *
 * ## Initialization order
 *
 * The caller MUST follow this sequence:
 *   1. setTickCallback(cb)   — register the tick handler
 *   2. start(midiPort)       — open the MIDI port and begin receiving
 *
 * Calling start() before setTickCallback() is legal but ticks will be
 * silently discarded until a callback is registered.
 *
 * ## Thread safety
 *
 * - getMtcMs() and isRunning() are safe to call from any thread at any time.
 * - setTickCallback() and start() are initialization-time methods and MUST
 *   NOT be called concurrently with each other or with getMtcMs()/isRunning().
 * - The registered tick callback fires from the RtMidi MIDI callback thread.
 *   The callback MUST be lock-free and non-blocking.
 *
 * ## One-instance constraint
 *
 * MtcReceiver (the underlying decoder) uses process-global static members.
 * Only one MtcTickSource may exist per process at a time.
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
     * The callback is invoked on every MTC quarter-frame decode, carrying
     * the current timecode head position in milliseconds. It fires at
     * 200 Hz for a 25 fps MTC stream (8 quarter frames per video frame).
     *
     * The callback MUST be registered before calling start(). Registering
     * a new callback after start() is undefined behavior.
     *
     * @param cb  Callable invoked with the current MTC head position (ms).
     *            MUST be lock-free and non-blocking — it fires from the
     *            RtMidi MIDI callback thread.
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
     * Enables network mode (MtcReceiver::setNetworkMode(true)) before
     * opening the port, so that network MTC sources (e.g., rtpmidid) work
     * without additional configuration.
     *
     * Port selection: scans available RtMidi ports for a port whose name
     * contains @p midiPort (case-sensitive substring match). If exactly
     * one match is found, that port is opened.
     *
     * @param midiPort  Port name (or substring) to search for and open.
     *
     * @return MtcStartError::kOk on success.
     *         MtcStartError::kNoPortsAvailable if no MIDI ports exist.
     *         MtcStartError::kPortNotFound if no port matches @p midiPort.
     *
     * @example
     * @code
     *   auto err = src.start("MTC");
     *   if (err != gme::time::MtcStartError::kOk) {
     *       logger.error("Failed to open MIDI port");
     *   }
     * @endcode
     */
    MtcStartError start(const std::string& midiPort);

    /**
     * @brief Return the current MTC head position in milliseconds.
     *
     * Reads MtcReceiver::mtcHead atomically. Safe to call from any thread
     * at any time, including before start().
     *
     * @return Current MTC head in milliseconds. Returns 0 if MTC has
     *         never started.
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
     * Reads MtcReceiver::isTimecodeRunning atomically. Returns false if
     * no quarter-frame messages have been received within the configured
     * running timeout (~100 ms default).
     *
     * @return true if MTC is actively running, false if stopped or not started.
     *
     * @example
     * @code
     *   if (src.isRunning()) { /* MTC is active */ }
     * @endcode
     */
    bool isRunning() const;
};

} // namespace time
} // namespace gme
