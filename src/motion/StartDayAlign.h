/*
 * ***
 * SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileContributor: Ion Reguera <ion@stagelab.coop>
 * ***
 */

/**
 * @file StartDayAlign.h
 * @brief Day alignment of an explicit fade start (24h MTC wrap).
 *
 * The engine sends `start_mtc_ms` in ITS 24h-extended MTC domain (the wraps
 * it has seen since its last reset); this daemon's MTC head counts the wraps
 * THIS process has seen. They differ after a daemon restart past 24 h, a load
 * at wire 0-1 h after a real wrap or at wire 23-24 h, so the fade froze (start
 * a day ahead) or cut at once (a day behind), with no log. An explicit start
 * is converted into this process's day with MtcReceiver::toLocalDay (nearest
 * day, 12 h rule); the `-1` sentinel ("now", FR-016) is checked first and
 * never converted. No clamp: FadeMotion uses the signed difference.
 *
 * Plan: cuems-RELATIONS Plans/2026-10-25-mtc-day-align-24h-wrap.md, step 8.
 */

#pragma once

#include <string>
#include "mtcreceiver/mtcreceiver.h"

namespace gme {
namespace motion {

enum class StartResolution {
    Sentinel,       ///< start_mtc_ms == -1: the current MTC
    Converted,      ///< an engine time placed in this process's day
    AsReceived      ///< the head was not authoritative: used as received (pre-fix behaviour)
};

constexpr long kHourMs = 3600000L;

/**
 * @brief Resolve a FadeCommand start into this process's MTC day.
 *
 * @param toLocalDay  bool(long engineMs, long& localMs, long& k), lock-free:
 *                    MtcTickSource::toLocalDay in the daemon, a fake in tests.
 */
template <typename ToLocalDay>
StartResolution resolveStart(long cmdStartMs, long mtcNowMs, ToLocalDay&& toLocalDay,
                             long& startMs, long& k) {
    k = 0;
    if (cmdStartMs == -1) {
        startMs = mtcNowMs;
        return StartResolution::Sentinel;
    }
    long localMs = 0;
    if (toLocalDay(cmdStartMs, localMs, k)) {
        startMs = localMs;
        return StartResolution::Converted;
    }
    k = 0;
    startMs = cmdStartMs;
    return StartResolution::AsReceived;
}

/** A start more than an hour ahead means a wrong day (or an engine time far from now). */
inline bool implausibleStart(long startMs, long mtcNowMs) { return startMs > mtcNowMs + kHourMs; }

/**
 * This daemon outlives node-engine restarts, so a negative k is expected once it
 * has seen a wrap of its own (head in day >= 1); with the head still in day 0
 * nothing explains the engine being a day behind.
 */
inline bool realignIsWarning(long k, long headMs) { return k < 0 && headMs < MtcReceiver::DAY_MS; }

inline std::string realignLine(long k, long engineMs, long headMs) {
    return "MTC day realign k=" + std::to_string(k) + " (engine " + std::to_string(engineMs)
           + " ms, local head " + std::to_string(headMs) + " ms, phase=ext)";
}

} // namespace motion
} // namespace gme
