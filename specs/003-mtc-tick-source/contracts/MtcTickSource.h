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
 */

#pragma once

#include <functional>
#include <string>
#include <stdexcept>

namespace gme {
namespace time {

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
     * one match is found, that port is opened. If no match is found, throws.
     *
     * @param midiPort  Port name (or substring) to search for and open.
     *
     * @throws std::runtime_error if no MIDI ports are available, no port
     *         matching @p midiPort is found, or the port fails to open.
     */
    void start(const std::string& midiPort);

    /**
     * @brief Return the current MTC head position in milliseconds.
     *
     * Reads MtcReceiver::mtcHead atomically. Safe to call from any thread
     * at any time, including before start().
     *
     * @return Current MTC head in milliseconds. Returns 0 if MTC has
     *         never started.
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
     */
    bool isRunning() const;
};

} // namespace time
} // namespace gme
