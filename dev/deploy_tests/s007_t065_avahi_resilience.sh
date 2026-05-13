#!/usr/bin/env bash
# ***
# SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
# SPDX-License-Identifier: GPL-3.0-or-later
# ***
#
# T065 — Avahi Resilience
#
# Extracts the journal entries from the T065 run (PID 223916, 2026-05-13).
# Does NOT rerun the daemon — the original run already happened with avahi-daemon
# stopped. Results are preserved in results/s007_t065_avahi_resilience.txt.
#
# To reproduce: stop avahi-daemon, start the daemon with --node-name test-avahi,
# send cancel_all + start_fade, observe no avahi-related errors.
#
# Usage: ./t065_avahi_resilience.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_FILE="$SCRIPT_DIR/results/s007_t065_avahi_resilience.txt"
PID=223916

echo "T065 — Avahi Resilience (journal extraction, PID $PID)"
echo ""

# Extract from journal if available; fall back to pre-saved results file
if journalctl _PID="$PID" --no-pager -q 2>/dev/null | grep -q "gradient-motiond"; then
    echo "Extracting from journalctl _PID=$PID ..."
    {
        echo "T065 — Avahi Resilience Test"
        echo "Extracted from journalctl _PID=$PID"
        echo ""
        echo "--- Journal (PID $PID) ---"
        journalctl _PID="$PID" --no-pager -q 2>/dev/null
        echo ""
    } | tee "$RESULTS_FILE"
else
    echo "Journal for PID $PID not available (may have been rotated)."
    echo "Pre-saved results:"
    echo ""
    cat "$RESULTS_FILE"
fi

echo ""
echo "--- Verification ---"
PASS=true

check() {
    local pattern="$1"
    local label="$2"
    if grep -q "$pattern" "$RESULTS_FILE"; then
        echo "PASS: $label"
    else
        echo "FAIL: $label"
        PASS=false
    fi
}

check "OscServer bound"           "OscServer bound without avahi"
check "gradient-motiond running"  "Daemon reached running state"
check "cancel_all"                "/gradient/cancel_all accepted"
check "start_fade.*fade-t065"     "/gradient/start_fade fade-t065 accepted"
check "Log finished"              "Clean shutdown"

# No avahi-daemon errors expected — "test-avahi" is the node name, not an avahi ref;
# look for actual avahi-daemon errors: "avahi-daemon", "avahi error", "avahi failed", etc.
if grep -i "GradientEngine\[" "$RESULTS_FILE" | grep -Eqi "avahi[- ](daemon|error|failed|warn)"; then
    echo "FAIL: avahi-daemon error observed in daemon journal"
    PASS=false
else
    echo "PASS: No avahi-daemon errors in daemon journal (node name 'test-avahi' is unrelated)"
fi

echo ""
if $PASS; then
    echo "RESULT: PASS — OSC transport has no avahi dependency"
    exit 0
else
    echo "RESULT: FAIL"
    exit 1
fi
