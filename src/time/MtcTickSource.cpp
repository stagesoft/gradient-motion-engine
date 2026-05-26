/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */

/**
 * @file MtcTickSource.cpp
 * @brief Implementation of gme::time::MtcTickSource.
 */

#include "time/MtcTickSource.h"

#include <climits>

namespace gme {
namespace time {

void MtcTickSource::setTickCallback(std::function<void(long)> cb) {
    if (cb) {
        MtcReceiver::setTickCallback(
            [cb = std::move(cb)](long ms, bool /*isCompleteFrame*/) {
                cb(ms);
            });
    } else {
        MtcReceiver::setTickCallback({});
    }
}

MtcTickSource::~MtcTickSource() {
    MtcReceiver::setTickCallback({});
}

MtcStartError MtcTickSource::start(const std::string& midiPort) {
    // Enable network mode before constructing MtcReceiver so that network
    // MTC sources (e.g., rtpmidid) work without additional configuration (FR-002).
    MtcReceiver::setNetworkMode(true);

    // Scan available ports for a name containing midiPort.
    // RtMidiIn() throws RtMidiError when the ALSA/MIDI subsystem is absent
    // (e.g., headless CI runners with no /dev/snd/seq).
    unsigned int portIndex = UINT_MAX;
    try {
        RtMidiIn probe;
        unsigned int nPorts = probe.getPortCount();
        if (nPorts == 0) {
            return MtcStartError::kNoPortsAvailable;
        }
        for (unsigned int i = 0; i < nPorts; ++i) {
            if (probe.getPortName(i).find(midiPort) != std::string::npos) {
                portIndex = i;
                break;
            }
        }
    } catch (const RtMidiError&) {
        return MtcStartError::kNoPortsAvailable;
    }
    if (portIndex == UINT_MAX) {
        return MtcStartError::kPortNotFound;
    }

    // Construct MtcReceiver — opens the port and starts the checker thread.
    // portIndex is the last constructor parameter (preserves positional compat).
    receiver_ = std::make_unique<MtcReceiver>(
        MTCRECV_DEFAULT_API, "Cuems Mtc Receiver", 100, portIndex);

    return MtcStartError::kOk;
}

long MtcTickSource::getMtcMs() const {
    return MtcReceiver::mtcHead.load();
}

bool MtcTickSource::isRunning() const {
    return MtcReceiver::isTimecodeRunning.load();
}

} // namespace time
} // namespace gme
