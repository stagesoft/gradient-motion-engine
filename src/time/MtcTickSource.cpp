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

// #region DEBUG — Phase 7 MTC-bias verification instrumentation.
// Dropped by Adrià pre-main-merge per commit-message scope prefix.
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sys/stat.h>
namespace {
void fade_dbg_log(long mtc_ms) {
    try {
        mkdir("/tmp/.claude", 0755);
        auto now_sys = std::chrono::system_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            now_sys.time_since_epoch()) % 1000000;
        auto t = std::chrono::system_clock::to_time_t(now_sys);
        std::tm tm_buf{};
        localtime_r(&t, &tm_buf);
        struct timespec mono{};
        clock_gettime(CLOCK_MONOTONIC, &mono);
        long long mono_ns = static_cast<long long>(mono.tv_sec) * 1000000000LL + mono.tv_nsec;
        std::ofstream f("/tmp/.claude/debug.log", std::ios::app);
        f << "[" << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
          << "." << std::setw(6) << std::setfill('0') << us.count()
          << "] [FADE] tick mtc_ms=" << mtc_ms
          << " clock_monotonic_ns=" << mono_ns << "\n";
    } catch (...) {}
}
}
// #endregion DEBUG

namespace gme {
namespace time {

void MtcTickSource::setTickCallback(std::function<void(long)> cb) {
    if (cb) {
        MtcReceiver::setTickCallback(
            [cb = std::move(cb)](long ms, bool isCompleteFrame) {
                if (isCompleteFrame) fade_dbg_log(ms);
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
    RtMidiIn probe;
    unsigned int nPorts = probe.getPortCount();
    if (nPorts == 0) {
        return MtcStartError::kNoPortsAvailable;
    }

    unsigned int portIndex = UINT_MAX;
    for (unsigned int i = 0; i < nPorts; ++i) {
        if (probe.getPortName(i).find(midiPort) != std::string::npos) {
            portIndex = i;
            break;
        }
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
