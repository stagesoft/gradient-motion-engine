<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Quickstart — Phase H: OSC Input Transport

**Feature**: [007-osc-input-transport](spec.md)
**Date**: 2026-05-13

This quickstart shows how to (1) build and run the refactored daemon locally, (2) drive it from a hand-written OSC client to verify the wire contract, and (3) reproduce the on-hardware Phase E failing scenario as a green check.

The plan and tasks for *implementing* the refactor live in [`plan.md`](plan.md) and (after `/speckit-tasks`) `tasks.md`. This document is for the implementer or reviewer once the code is in place.

## Prerequisites

- Debian 13 / Ubuntu 24.04 development host with `liblo-dev`, `nlohmann-json3-dev`, `librtmidi-dev`, `cmake`, `g++` ≥ 11.
- The `mtcreceiver` and `cuemslogger` git submodules initialized (`git submodule update --init --recursive` from the repo root).
- A working `python3` with `python-osc` available (for the wire-level smoke test). `pip install python-osc` if missing.

## 1. Build the refactored daemon

From the repo root:

```bash
cmake -S . -B build_daemon -DBUILD_DAEMON=ON
cmake --build build_daemon -j
```

Expect:

- Zero references to NNG in the build output. `cmake --build` does not invoke `find_package(nng)`.
- A produced binary at `build_daemon/daemon/gradient-motiond` (or wherever the existing CMake graph places it).

Confirm the libnng dependency is gone:

```bash
ldd build_daemon/daemon/gradient-motiond | grep -i nng    # must print nothing
```

This corresponds to spec **SC-004**.

## 2. Run the unit tests

```bash
cmake --build build_daemon --target test_osc_parse test_osc_server_integration \
    test_motion_registry test_fade_motion test_curves test_lockfree_queue \
    test_mtc_tick_source
ctest --test-dir build_daemon --output-on-failure
```

Expect green on all targets. The new tests are `test_osc_parse` and `test_osc_server_integration`; the others are pre-existing and must remain unchanged (spec **SC-006**).

## 3. Run the daemon locally

```bash
./build_daemon/daemon/gradient-motiond --osc-port 7100 --node-name node-dev --log-level debug
```

The daemon should log a startup line naming the bound port and the node name, then sit waiting. Verify the socket is bound to loopback only:

```bash
ss -ulnp | grep 7100
# Expect: UNCONN 0 0 127.0.0.1:7100 0.0.0.0:* users:(("gradient-motiond",...))
```

If the bind shows `0.0.0.0:7100` instead of `127.0.0.1:7100`, that's a regression on spec **FR-002** — file a bug, do not deploy.

## 4. Wire-level smoke from a hand-written OSC client

A tiny Python script that drives the daemon without involving cuems-engine. Save as `/tmp/smoke.py`:

```python
#!/usr/bin/env python3
"""Hand-driven OSC client for verifying gradient-motiond's /gradient/* wire."""
from pythonosc.udp_client import SimpleUDPClient

c = SimpleUDPClient("127.0.0.1", 7100)

# /gradient/start_fade — sssisffhisss
c.send_message("/gradient/start_fade", [
    "fade-quickstart",      # motion_id
    "node-dev",             # node_name
    "127.0.0.1",            # osc_host (downstream player)
    9001,                   # osc_port
    "/volmaster",           # osc_path
    0.0,                    # start_value
    1.0,                    # end_value
    0,                      # start_mtc_ms (h)
    2000,                   # duration_ms (i)
    "linear",               # curve_type
    "{}",                   # curve_params_json
])

# Wait, then cancel
import time; time.sleep(0.5)
c.send_message("/gradient/cancel_motion", ["fade-quickstart", "node-dev"])
```

Send it:

```bash
python3 /tmp/smoke.py
```

In the daemon journal you should see (with `--log-level debug`):

```
[debug] OscServer: /gradient/start_fade accepted motion_id=fade-quickstart
[debug] MotionFactory: built FadeMotion for fade-quickstart
[info]  MotionRegistry: motion fade-quickstart added (start_mtc_ms=0 duration_ms=2000)
[debug] OscServer: /gradient/cancel_motion accepted motion_id=fade-quickstart
[info]  MotionRegistry: motion fade-quickstart cancelled
```

This validates spec **FR-001 / FR-003 / FR-005 / FR-006**.

### Negative-path smoke (node_name mismatch)

```python
c.send_message("/gradient/start_fade", [
    "fade-mismatch", "node-other",     # <- wrong node name
    "127.0.0.1", 9001, "/volmaster",
    0.0, 1.0, 0, 2000, "linear", "{}",
])
```

Daemon journal:

```
[debug] OscServer: dropped fade-mismatch — node_name mismatch (expected node-dev, got node-other)
```

No `MotionRegistry` entry created. Validates **FR-004**.

### Negative-path smoke (malformed JSON in curve_params)

```python
c.send_message("/gradient/start_fade", [
    "fade-bad-json", "node-dev",
    "127.0.0.1", 9001, "/volmaster",
    0.0, 1.0, 0, 2000, "bezier", "{this is not json",
])
```

Daemon journal:

```
[warn] OscServer: parse failed motion_id=fade-bad-json — TypeError on curve_params_json
```

Daemon keeps running. Validates **FR-006**.

## 5. End-to-end on node-002 (the Phase E failing scenario)

This is the spec **SC-001** acceptance check. Run on the actual node-002 deploy.

Prerequisite: `feat/gradient-osc-transport` of `cuems-engine` is installed (editable or via the staging deb), `cuems-common` has the updated systemd unit (no `--nng-url`, no Avahi `Wants=`), and the gradient-motion-engine deb is rebuilt from this branch.

```bash
sudo systemctl restart cuems-controller-engine cuems-node-engine cuems-gradient-motiond
ss -ulnp | grep 7100                           # confirm daemon listening on loopback
journalctl -fu cuems-gradient-motiond &        # tail in another terminal
```

In the editor UI, open `/opt/cuems_library/projects/fade-test/` (the project used during Phase E: one AudioCue + one FadeCue, target_value=0, duration 5s, curve linear). Press GO.

Expected:

1. Audio fades smoothly to silence over 5 s. No audible bounce.
2. Daemon journal shows the accepted `/gradient/start_fade`, ~100 `/volmaster` OSC ticks emitted on the output side, and no NNG warnings.
3. No reference to `controller.local` anywhere in the daemon's startup or runtime logs.
4. Pressing STOP mid-fade clears the registry (`OscServer: /gradient/cancel_all accepted`).

If any of (1)–(4) fail, the spec acceptance is not met. Do not declare Phase H complete.

### Optional: Avahi-stopped resilience check (spec SC-003)

```bash
sudo systemctl stop avahi-daemon
sudo systemctl restart cuems-gradient-motiond
journalctl -u cuems-gradient-motiond -n 50
```

The daemon must come up cleanly with no Avahi errors and no waits. Re-run the fade-test GO; it must still play correctly. Validates **FR-007 / FR-013**.

## 6. Verify spec 005 is marked superseded

```bash
head -n 30 specs/005-nng-bus-client/spec.md
```

Expect a `Superseded by` line pointing to `specs/007-osc-input-transport/spec.md` and an explanatory block listing which sub-elements of 005 remain valid (FadeCommand struct, LockFreeQueue, node_name filter, shutdown contract) versus invalidated (NNG dial / listener wiring, status envelope, `target: "gradientengine"` shape). Validates **FR-015 / SC-007**.

## 7. Cleanup

```bash
# stop the local daemon if still running
pkill -INT gradient-motiond
rm /tmp/smoke.py
```

## What you have demonstrated

| Spec ref | Check | Where verified |
|---|---|---|
| FR-001 | OSC port accepts `/gradient/*` commands | step 4 |
| FR-002 | Loopback-only bind | step 3 (`ss` output) |
| FR-003 | Three-message command set works | step 4 |
| FR-004 | `node_name` filter drops cross-node traffic | step 4 negative |
| FR-006 | Malformed messages do not crash the daemon | step 4 negative |
| FR-007 / FR-013 | Daemon starts with Avahi stopped | step 5 optional |
| FR-008 | No status broadcasts emitted | inspect: no outbound packets on any non-volmaster socket |
| FR-012 | No libnng runtime dep on binary | step 1 (`ldd`) |
| FR-015 | Spec 005 marked superseded | step 6 |
| SC-001 | 5 s fade on node-002 plays cleanly | step 5 |
| SC-006 | All prior tests still green | step 2 |

Plan / tasks / implementation details live in the surrounding files in this spec directory.
