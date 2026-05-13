#!/usr/bin/env bash
# ***
# SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
# SPDX-License-Identifier: GPL-3.0-or-later
# ***
#
# T052 — OSC Send-Failure Rate-Limit Check
#
# Verifies kOscFailureThreshold = 5: after 5 consecutive OSC status-send failures
# the MotionRegistry removes the fade and emits MotionError:"osc_send_failed".
#
# IMPORTANT LIMITATION on dev host:
#   The threshold is only reached when MotionRegistry tries to send OSC status
#   back on a per-tick basis — which requires MTC ticks to drive the engine.
#   On the dev host there is no real MTC source (virtual MIDI gives MIDI Through
#   but no MTC stream), so MotionRegistry.tick() is not called from the daemon
#   after start_fade is accepted. The test therefore verifies:
#     1. Daemon starts and accepts start_fade without crashing (acceptance path)
#     2. No immediate crash on a closed callback port
#   Full threshold verification (kOscFailureThreshold = 5 eviction) requires
#   a live MTC stream and must be run on node-002 with a real MTC source.
#
# For node-002 (full test):
#   1. Start a MTC source (e.g., aplaymidi with a timecode file)
#   2. Set cb_port to a closed port (e.g., 19999) so every status send fails
#   3. After 5 ticks the registry must evict the fade and log:
#      "MotionRegistry: removed fade-id — osc_send_failed after 5 attempts"
#   4. Verify no further ticks produce errors for that motion_id
#
# Usage: ./t052_rate_limit.sh [port]   (default: 17101)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
RESULTS_FILE="$SCRIPT_DIR/results/s007_t052_rate_limit.txt"
DAEMON="$REPO_ROOT/build_daemon/gradient-motiond"
OSC_SENDER="$SCRIPT_DIR/s007_osc_sender"
PORT="${1:-17101}"
NODE="rate-limit-node"
MIDI_PORT="Midi Through Port-0"
CLOSED_CB_PORT=19999   # nothing listening here — status sends will fail

PASS=true
DAEMON_PID=""

fail() { echo "FAIL: $1"; PASS=false; }
pass() { echo "PASS: $1"; }
skip() { echo "SKIP: $1 (requires MTC — see node-002 instructions above)"; }

cleanup() {
    if [[ -n "$DAEMON_PID" ]] && kill -0 "$DAEMON_PID" 2>/dev/null; then
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

{

echo "T052 — OSC Send-Failure Rate-Limit Check"
echo "Date: $(date)"
echo "Port: $PORT  Node: $NODE  Closed-CB-Port: $CLOSED_CB_PORT"
echo ""
echo "NOTE: Full kOscFailureThreshold=5 eviction test requires MTC ticks (node-002)."
echo "      This script verifies the acceptance path and crash-free behaviour."
echo ""

# ── Prerequisites ────────────────────────────────────────────────────────────

echo "--- Prerequisites ---"
if [[ ! -x "$DAEMON" ]]; then
    echo "FATAL: daemon binary not found at $DAEMON"
    exit 1
fi
pass "daemon binary exists"

if [[ ! -x "$OSC_SENDER" ]]; then
    echo "Building s007_osc_sender ..."
    g++ -std=c++17 "$SCRIPT_DIR/s007_osc_sender.cpp" -llo -o "$OSC_SENDER"
fi
pass "s007_osc_sender built"

echo ""

# ── Start daemon ─────────────────────────────────────────────────────────────

echo "--- Start daemon ---"
"$DAEMON" \
    --midi-port "$MIDI_PORT" \
    --osc-port  "$PORT"      \
    --node-name "$NODE"      \
    --log-level debug        \
    &
DAEMON_PID=$!
echo "Daemon PID: $DAEMON_PID"
sleep 0.5

if ! kill -0 "$DAEMON_PID" 2>/dev/null; then
    echo "FATAL: daemon exited immediately"
    exit 1
fi
pass "Daemon running (PID $DAEMON_PID)"

echo ""

# ── Send start_fade with closed callback port ────────────────────────────────

echo "--- Send start_fade with closed callback port ($CLOSED_CB_PORT) ---"
MOTION_ID="t052-fade-1"
"$OSC_SENDER" "$PORT" start_fade \
    "$MOTION_ID" "$NODE" "127.0.0.1" "$CLOSED_CB_PORT" "/status" \
    0.0 1.0 0 5000 "linear" "{}"
sleep 0.5

if kill -0 "$DAEMON_PID" 2>/dev/null; then
    pass "Daemon still running after start_fade to closed callback port (no crash)"
else
    fail "Daemon crashed after start_fade with closed callback port"
fi

if journalctl -n 50 --no-pager _PID="$DAEMON_PID" 2>/dev/null \
   | grep -q "accepted /gradient/start_fade motion_id=$MOTION_ID"; then
    pass "start_fade accepted: $MOTION_ID"
else
    fail "start_fade not accepted — journal check failed"
fi

echo ""

# ── MTC-dependent eviction check ────────────────────────────────────────────

echo "--- kOscFailureThreshold eviction (requires MTC ticks) ---"
skip "Cannot verify 5-failure eviction without MTC source on dev host"
echo ""
echo "For node-002 verification:"
echo "  1. Start MTC source (aplaymidi or hardware MTC)"
echo "  2. Run: ./t052_rate_limit.sh (same script, MTC drives ticks)"
echo "  3. Expected log: MotionError on osc_send_failed after 5 consecutive failures"
echo "  4. kOscFailureThreshold = 5 is defined in src/engine/MotionRegistry.h"

echo ""

# ── Shutdown ─────────────────────────────────────────────────────────────────

echo "--- Shutdown ---"
kill "$DAEMON_PID"
wait "$DAEMON_PID" 2>/dev/null || true
DAEMON_PID=""
sleep 0.2
pass "Daemon shut down cleanly"

echo ""
echo "--- Summary ---"
echo "Dev-host verification: $( $PASS && echo PASS || echo FAIL )"
echo "Full threshold test:    SKIP — requires MTC on node-002"
if $PASS; then
    echo "RESULT: PASS (dev-host scope)"
else
    echo "RESULT: FAIL"
fi

} 2>&1 | tee "$RESULTS_FILE"

$PASS && exit 0 || exit 1
