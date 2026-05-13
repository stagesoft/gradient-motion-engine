<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Wire Contract — `/gradient` OSC namespace (Phase H, canonical)

**Feature**: [007-osc-input-transport](../spec.md)
**Date**: 2026-05-13
**Supersedes**: The NodeOperation JSON envelope in [specs/005-nng-bus-client/contracts/FadeCommand.h](../../005-nng-bus-client/contracts/FadeCommand.h) for *inbound* commands. The C++ `FadeCommand` struct itself is unchanged — only the wire format changes.

This document is the canonical contract for the OSC messages that cuems-engine (`GradientPlayer`) sends to `gradient-motiond` and that the daemon parses.

## Transport

- **Protocol**: UDP/IPv4.
- **Bind**: daemon binds **only** to `127.0.0.1` on a configurable port (default `7100`). Loopback only — never publishes on a routable interface.
- **Direction**: one-way. NodeEngine → daemon. The daemon does **not** send any OSC back over this socket.
- **Concurrency**: messages MAY arrive interleaved from different threads on the sender side; the daemon processes them sequentially on its liblo server thread.
- **Reliability**: no retransmit. UDP packet loss on loopback in steady-state CUEMS deployments is effectively zero (measured nanoseconds latency, OS kernel queue depth >> burst size). Senders MUST treat the OSC send as fire-and-forget.

## Address space

Three OSC addresses make up the v1 contract:

| Address | Purpose |
|---|---|
| `/gradient/start_fade` | Start (or supersede) a fade. |
| `/gradient/cancel_motion` | Cancel a single fade by `motion_id`. |
| `/gradient/cancel_all` | Cancel every fade in the daemon's registry. |

The `/gradient/` prefix is reserved for the daemon's command surface. Any future commands (e.g. `/gradient/start_crossfade`, `/gradient/start_vector`) live under the same prefix and are out of scope for Phase H.

## Message: `/gradient/start_fade`

### Type tag

```text
,sssisffhisss
```

Eleven arguments. Order is positional and fixed.

### Arguments

| # | Type tag | Name | OSC type | C++ field (`FadeCommand`) | Notes |
|---|---|---|---|---|---|
| 0 | `s` | `motion_id` | string | `motion_id` | Caller-assigned unique id. Used for supersede + cancel-by-id. Non-empty. |
| 1 | `s` | `node_name` | string | `node_name` | Daemon drops the message if this does not match its configured node name (FR-004). |
| 2 | `s` | `osc_host` | string | `osc_host` | Downstream player host (e.g. `"127.0.0.1"`). |
| 3 | `i` | `osc_port` | int32 | `osc_port` | Downstream player UDP port. 1–65535. |
| 4 | `s` | `osc_path` | string | `osc_path` | Downstream OSC address (e.g. `"/volmaster"`). Must start with `/`. |
| 5 | `f` | `start_value` | float32 | `start_value` | Curve domain start. Typically 0.0–1.0 in OSC space. |
| 6 | `f` | `end_value` | float32 | `end_value` | Curve domain end. |
| 7 | `h` | `start_mtc_ms` | int64 | `start_mtc_ms` (`uint64_t`) | Absolute MTC timestamp in ms. Must be non-negative when interpreted as int64. |
| 8 | `i` | `duration_ms` | int32 | `duration_ms` (`uint32_t`) | Fade length in ms. Must be > 0. |
| 9 | `s` | `curve_type` | string | `curve_type` | One of the curve names registered with `gme::gradient::CurveFactory`. Phase H does not validate the value at the OSC layer — `MotionFactory` rejects unknown types downstream. |
| 10 | `s` | `curve_params_json` | string | `curve_params` (`nlohmann::json`) | JSON object literal, e.g. `'{"p1":[0.25,0.1],"p2":[0.25,1.0]}'`. May be the empty object `"{}"`. Parsed by `nlohmann::json::parse`; parse failure → `TypeError`. Unknown keys are ignored. |

### Semantics

- **Supersede**: if a fade with the same `motion_id` is already active, the daemon's `MotionRegistry::addMotion` replaces it (existing supersede semantics from feature 006). The old fade's last-sent value is held; the new fade begins evaluation at its own `start_mtc_ms`.
- **Late arrival**: same behavior as Phase 3 — if `start_mtc_ms` has already elapsed, the registry's existing tick-loop handles the late case (clamps to the curve's late position).
- **Reject conditions** (per Decision 7 / parser contract):
  - Type tag does not match `,sssisffhisss` → `TypeError`, drop, log warning naming the offending arg index.
  - `node_name` (arg 1) does not match the daemon's `--node-name` → `NodeMismatch`, drop, log debug only.
  - Any required string is empty, or `osc_port` ≤ 0, or `duration_ms` ≤ 0 → `MissingField`, drop, log warning.
  - `curve_params_json` not parseable as a JSON object → `TypeError`, drop, log warning naming `motion_id`.

### Example

```text
Address: /gradient/start_fade
Tag:     ,sssisffhisss
Args:    "fade-7a3c1"  "node-002"  "127.0.0.1"  9001  "/volmaster"
         0.0  1.0  86400123  5000  "bezier"  "{\"p1\":[0.25,0.1],\"p2\":[0.25,1.0]}"
```

This requests: on node-002, fade `/volmaster` on `127.0.0.1:9001` from 0.0 to 1.0 over 5000 ms starting at MTC 86,400,123 ms, using a bezier curve.

## Message: `/gradient/cancel_motion`

### Type tag

```text
,ss
```

### Arguments

| # | Type tag | Name | Notes |
|---|---|---|---|
| 0 | `s` | `motion_id` | The id of the motion to cancel. Non-empty. |
| 1 | `s` | `node_name` | Defense-in-depth filter (FR-004). |

### Semantics

- If a fade with this `motion_id` is active, the daemon removes it from the registry. The target parameter is held at its last-sent value (no final OSC tick is emitted).
- If no such fade exists, the message is silently discarded (no error).
- Type-tag mismatch → drop + warning. `node_name` mismatch → drop + debug log.

### Example

```text
Address: /gradient/cancel_motion
Tag:     ,ss
Args:    "fade-7a3c1"  "node-002"
```

## Message: `/gradient/cancel_all`

### Type tag

```text
,s
```

### Arguments

| # | Type tag | Name | Notes |
|---|---|---|---|
| 0 | `s` | `node_name` | Defense-in-depth filter (FR-004). |

### Semantics

- Every active fade in the daemon's registry is removed. All target parameters held at their last-sent values.
- Used by NodeEngine on STOP and on LOAD (FR-013).
- Daemon shutdown (SIGTERM/SIGINT) is internally treated as `cancel_all` (FR-011); operators do not need to send this explicitly.

### Example

```text
Address: /gradient/cancel_all
Tag:     ,s
Args:    "node-002"
```

## Versioning & forward compatibility

- The wire format has no embedded version field. v1 is locked by this document.
- v2 messages MUST use a new address or extend `start_fade` with additional **trailing** arguments. New trailing args MUST be optional from the daemon's perspective (i.e. the daemon SHOULD accept v1's argument count via partial type-tag match). This is deferred — Phase H ships strict-equal type-tag matching only.
- Unknown OSC addresses delivered to the daemon's port are logged at debug level and dropped (default liblo behavior is to ignore them; explicit catch-all method registration is not added in Phase H).
- Unknown keys inside `curve_params_json` are silently ignored (forward-compatibility with new curve types — same rule as Phase 3, FR-014 in spec 005).

## Out-of-scope wire shapes (deferred)

The following addresses are **not** part of the Phase H contract. They are listed here so the namespace stays reserved for them:

- `/gradient/start_crossfade` — Phase 7. Argument list: `start_fade` args plus a paired set with `partner_*` prefix.
- `/gradient/start_vector` — future N-dimensional motions (`VectorMotion<2..4>`). Argument list: TBD when those motions are designed.
- Any reverse-direction `/gradient/status/*` messages — explicitly out of scope (spec FR-008). Fade lifecycle is reported by NodeEngine's `loop_fadeCue` in cuems-engine, not by the daemon.
