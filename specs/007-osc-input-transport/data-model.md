<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Data Model — Phase H: OSC Input Transport

**Feature**: [007-osc-input-transport](spec.md)
**Date**: 2026-05-13

This feature is transport-only. It introduces **no new persistent state**, **no new domain entities**, and **no schema changes** to the motion / curve / registry data model. What changes is the *origin* of one already-existing in-memory record (`FadeCommand`) and the wrapper that owns the network thread that produces it.

## Entities

### 1. `FadeCommand` (existing struct — Phase H renames `fade_id` → `motion_id`)

Defined in [`src/signal/FadeCommand.h`](../../src/signal/FadeCommand.h). The struct's *shape* is preserved (same set of fields, same types). Two field names are renamed in Phase H Foundational work for ecosystem consistency, since "fades are a type of motion" and every future motion type will key on `motion_id`:

| Field (Phase H) | Was (pre-H) | Type | Origin (Phase H) | Notes |
|---|---|---|---|---|
| `type` | `type` | `enum Type { START_FADE, CANCEL_MOTION, CANCEL_ALL, START_CROSSFADE }` | `OscServer` method dispatch | `CANCEL_MOTION` is the **canonical enum value** (already in `FadeCommand.h`); kept as-is so future motion types can reuse the cancel-by-motion-id command. `START_CROSSFADE` stays dormant. |
| `motion_id` | `fade_id` | `std::string` | OSC arg 0 of `start_fade` / `cancel_motion` | Renamed Phase H. The struct field, every docstring, and every test that mentions it gets renamed in the Foundational rename task. |
| `partner_motion_id` | `partner_fade_id` | `std::string` | unused in Phase H | Renamed Phase H for consistency. Stays dormant until the future crossfade feature lands. |
| `node_name` | `node_name` | `std::string` | OSC arg | Filter target (spec FR-004) |
| `osc_host` | `osc_host` | `std::string` | OSC arg | Downstream player host |
| `osc_port` | `osc_port` | `int` | OSC arg | Downstream player port |
| `osc_path` | `osc_path` | `std::string` | OSC arg | Downstream OSC address |
| `start_value` | `start_value` | `float` | OSC arg | Curve domain start |
| `end_value` | `end_value` | `float` | OSC arg | Curve domain end |
| `start_mtc_ms` | `start_mtc_ms` | `uint64_t` | OSC arg (`h` int64, cast to uint64) | MTC absolute timestamp |
| `duration_ms` | `duration_ms` | `uint32_t` | OSC arg (`i` int32, cast to uint32) | Fade length |
| `curve_type` | `curve_type` | `std::string` | OSC arg | "linear", "bezier", … |
| `curve_params` | `curve_params` | `nlohmann::json` | OSC arg (string, parsed) | Heterogeneous; stored as JSON object |
| `partner_*` (other) | `partner_*` (other) | (matching set) | unused in Phase H | Future crossfade feature |

**Lifetime**: created on the OSC server thread by `parseFadeOscCommand`, moved into a `LockFreeQueue<FadeCommand, 64>`, drained on the tick thread by `GradientEngine::onTick`, consumed by `MotionFactory` to instantiate `IMotion` subclasses.

**No persistence.** Lives entirely in memory.

### 2. `ParseResult` (existing — unchanged)

Defined in [`src/signal/FadeCommand.h`](../../src/signal/FadeCommand.h). The OSC parser reuses the same enum:

- `Ok` — command accepted, `FadeCommand` populated, enqueue.
- `NodeMismatch` — `node_name` field does not match this daemon's configured name. Drop silently (log debug only).
- `TargetMismatch` — *unused* by OSC parser (no `target` envelope in OSC). The enum value is retained for source compatibility; the OSC parser never returns it.
- `MissingField` — required OSC arg or `curve_params` field missing. Log warning, drop command.
- `TypeError` — OSC type tag mismatch or `curve_params_json` not parseable. Log warning with `motion_id` if known, drop command.

### 3. `OscServer` (new — owns a thread; not a domain entity)

Wrapper class in `daemon/comms/OscServer.{cpp,h}`. Not part of `libgradient_motion` — daemon-only.

**Internal state**:

| Field | Type | Purpose |
|---|---|---|
| `lo_server_thread server_` | `lo_server_thread` (liblo opaque handle) | Owns the network thread. Created with `lo_server_thread_new(port, errorHandler)` bound to `127.0.0.1`. |
| `std::string node_name_` | `const std::string` | Captured at construction. Passed to `parseFadeOscCommand` on every call. |
| `gme::signal::LockFreeQueue<FadeCommand, 64>* out_queue_` | non-owning pointer | Where parsed commands go. Owned by `GradientEngine`. |

**Public surface** (full docstrings in the contract file):

```cpp
class OscServer {
public:
    OscServer(int port, std::string node_name, LockFreeQueue<FadeCommand, 64>* out);
    ~OscServer();                          // stops server, joins thread
    OscServer(const OscServer&) = delete;
    OscServer& operator=(const OscServer&) = delete;

    void start();                           // begin processing; non-blocking
    void stop();                            // request shutdown; idempotent
    int getPort() const noexcept;
};
```

**Threading**:

- Owns one liblo network thread (created by `lo_server_thread_new`).
- The server's per-address callbacks (`/gradient/start_fade`, etc.) run on that thread.
- Each callback parses, validates (incl. `node_name` filter), and pushes to `out_queue_` via `LockFreeQueue::push` (SPSC; wait-free on the producer side).
- `stop()` calls `lo_server_thread_stop` + `lo_server_thread_free` from the daemon shutdown thread.

### 4. `LockFreeQueue<FadeCommand, 64>` (existing — unchanged)

Defined in [`src/signal/LockFreeQueue.h`](../../src/signal/LockFreeQueue.h). Single-producer / single-consumer fixed-capacity ring buffer. Phase H switches the producer side from the NNG client thread to the liblo server thread; the queue itself is unchanged.

**Capacity**: 64 commands. Same as feature 005. Overflow behavior (push returns `false`) is preserved — the callback logs a warning and drops the command. Capacity-tuning is out of scope.

## State transitions

The transport flip does not introduce or modify state machines. `MotionRegistry`'s supersede / cancel / cancel-all semantics from feature 006 apply unchanged. The only state-transition relevant to Phase H is the OSC server lifecycle, which is trivial:

```
CREATED ── start() ──> RUNNING ── stop() ──> STOPPED ── ~OscServer ──> destroyed
```

## Configuration data

The daemon resolves `osc_port` in this order (Decision 5 in research.md):

1. `--osc-port <port>` CLI flag.
2. `CUEMS_GRADIENT_OSC_PORT` environment variable.
3. `/etc/cuems/settings.xml` → `<gradient_osc_port>` element (read via existing `Config` class — Phase H adds the element to the config parser; cuems-utils settings.xsd is updated correspondingly in the cuems-engine companion branch).
4. Compile-time default `7100`.

`node_name` continues to come from `--node-name` (existing flag) with fallback to `/etc/cuems/settings.xml`'s existing node identity element (already used since feature 005).

## What changes outside this repo

For traceability — the spec's "Out of scope" section names companion changes in `cuems-engine` (NodeEngine dispatch, cancel-on-STOP/LOAD, resilience, lifecycle reporting). Those are not entities in this repo's data model, but they consume and produce the OSC wire format defined in [`contracts/gradient_osc.md`](contracts/gradient_osc.md):

- `cuemsengine.players.GradientPlayer` (new) — sends `/gradient/*` messages over UDP.
- `cuemsengine.cues.ActionHandler._handle_fade_action` — dispatches via `GradientPlayer` instead of `send_fade_command`.
- `cuemsengine.NodeEngine` — calls `gradient_player.send_cancel_all()` on STOP and on LOAD.
- `cuemsengine.ControllerEngine` — drops `_send_gradient_cancel_all`.

These are documented in the spec's FR list and in the cross-repo handoff notes; the source of truth for the wire format is this repo's contract file.
