# Feature Specification: Phase 4 — Fade Registry, Tick Loop & OSC Sender

**Feature Branch**: `006-fade-registry-tick-loop`  
**Created**: 2026-04-23  
**Status**: Draft  
**Input**: User description: "implement Phase 4 as layed out in @dev/C++ Node-Level GradientEngine — Implementation Plan.md"

## Clarifications

### Session 2026-04-23

- Q: Is crossfade execution in scope for Phase 4, or deferred to Phase 7? → A: Phase 4 includes the crossfade data shape (partner pointer on `ActiveFade`, `CrossfadePair` hook in the registry) but NOT `START_CROSSFADE` command handling or paired-evaluation semantics — those remain deferred to Phase 7.
- Q: How should the registry resolve overlapping fades on the same `(host, port, path)` target? → A: Auto-cancel the prior fade (no final OSC value), emit a `fade_error` with `reason: "superseded"`, then register the new fade.
- Q: Where is the `Curve` object (including `ResampledCurve` LUT) constructed? → A: Inside `FadeRegistry::addFade` on the tick thread, after the `FadeCommand` is drained from the `LockFreeQueue`. The NNG recv thread only parses JSON and enqueues the raw command; the tick thread owns all curve allocation and `ActiveFade` materialization.
- Q: How is the `start_mtc_ms = -1` sentinel resolved? → A: Inside `FadeRegistry::addFade` on the tick thread, by reading `MtcTickSource::getMtcMs()` at the moment the command is applied and storing the concrete value in `ActiveFade::start_mtc_ms`. The `-1` sentinel never appears in `ActiveFade`.
- Q: What is the tick-thread real-time budget and max concurrent active fades? → A: The tick function MUST handle up to 50 concurrent active fades within a 2 ms wall-clock budget per tick (leaving ≥3 ms headroom at 200 Hz / 25 fps MTC).

## User Scenarios & Testing *(mandatory)*

### User Story 1 — MTC-Driven Fade Execution (Priority: P1)

A live show operator triggers an ActionCue with `fade_in`. The GradientEngine evaluates the active fade at each MTC quarter-frame tick, computes the current curve value, and sends the interpolated gain level over OSC to AudioPlayer or VideoComposer. The operator observes a smooth parametric volume or opacity transition synchronized to timecode.

**Why this priority**: Core functional delivery — without per-tick evaluation and OSC output, no fade reaches the audio or video player. All downstream features depend on this working correctly.

**Independent Test**: Spin up a mock MTC source and an OSC listener, inject a `FadeCommand` into the registry, advance MTC time, and verify the listener receives OSC messages at values that match the expected curve at each position.

**Acceptance Scenarios**:

1. **Given** a `FadeCommand` with `curve_type=sigmoid`, `duration_ms=3000`, `start_value=0.0`, `end_value=1.0` is registered, **When** MTC advances from `start_mtc_ms` to `start_mtc_ms + 1500`, **Then** the OSC message value is within ±0.005 of `sigmoid.evaluate(0.5)` scaled to [0, 1].
2. **Given** a running fade, **When** MTC reaches `start_mtc_ms + duration_ms`, **Then** one final OSC message with `end_value` is sent and the fade is marked completed and removed.
3. **Given** a completed fade, **When** MTC rewinds below `start_mtc_ms`, **Then** the fade is NOT replayed (it was already removed from the registry).

---

### User Story 2 — Fade Completion Status Notification (Priority: P1)

When a fade reaches `t = 1.0`, the system notifies the Controller engine via NNG that the fade completed (`fade_complete` event), so the Controller can trigger downstream actions such as disarming the cue target.

**Why this priority**: Without the completion signal, `fade_out` cannot trigger disarm, breaking the ActionCue lifecycle. Must be delivered out-of-band from the MTC tick thread to avoid blocking real-time processing.

**Independent Test**: Inject a short `FadeCommand` (e.g. 100 ms), advance MTC past the end, and verify a `fade_complete` NNG status message is enqueued and emitted by the status worker with the correct `fade_id`.

**Acceptance Scenarios**:

1. **Given** a fade completes (`t >= 1.0`), **When** the status worker drains its queue, **Then** a `fade_complete` NNG message is sent with the correct `fade_id` and `data.event = "fade_complete"`.
2. **Given** the MTC tick thread places a completion tuple on the status queue, **When** the status worker is busy, **Then** the tick thread is never blocked (SPSC drop-oldest on overflow).

---

### User Story 3 — Cancel Fade with Snap or Hold (Priority: P2)

An operator issues a `cancel_fade` command (e.g. project stop, manual abort). The system stops the fade immediately and either snaps the OSC parameter to the fade's `end_value` (for stop/disarm scenarios) or leaves it at the last sent value (for manual cancel).

**Why this priority**: Graceful cancellation prevents stale fade parameters on player restart and is required for `CANCEL_ALL` on project unload.

**Independent Test**: Register a fade, advance MTC partway, cancel with `snap_to_end=true`, verify one final OSC message at `end_value`. Repeat with `snap_to_end=false`, verify no additional OSC message is sent.

**Acceptance Scenarios**:

1. **Given** a fade is active at `t = 0.4`, **When** `cancelFade(id, snap_to_end=true)` is called, **Then** one OSC message at `end_value` is sent and the fade is removed.
2. **Given** a fade is active, **When** `cancelAll()` is called (project unload), **Then** all fades are removed and no final OSC values are sent.

---

### User Story 4 — OSC Send Failure Reporting (Priority: P2)

If repeated OSC sends to an audio or video player fail (e.g. player crashed mid-fade), the system emits a `fade_error` NNG status message with `reason: "osc_send_failed"` so the Controller knows the fade did not complete successfully.

**Why this priority**: Enables the Controller to handle failure gracefully (e.g. log, clear fade state) rather than waiting indefinitely for a `fade_complete` that will never arrive.

**Independent Test**: Point the OSC target at a closed port, verify that after a configurable consecutive-failure threshold the status worker emits a `fade_error` with `reason: "osc_send_failed"`.

**Acceptance Scenarios**:

1. **Given** OSC sends to the target port repeatedly fail, **When** the failure count exceeds the threshold, **Then** a `fade_error` status is enqueued with `reason: "osc_send_failed"` and the fade is removed.
2. **Given** a transient OSC failure followed by recovery, **When** fewer than the threshold consecutive failures occur, **Then** no `fade_error` is emitted and the fade continues.

---

### User Story 5 — MTC Pause, Resume, Rewind Behavior (Priority: P2)

When MTC transport stops mid-fade, the fade holds at `last_sent_value` automatically. When MTC resumes, the fade continues from the correct MTC-relative position. On rewind, the fade re-evaluates from the rewound position (clamped to `start_value` if before `start_mtc_ms`).

**Why this priority**: MTC transport events are common in live rehearsals. Correct behavior prevents jarring jumps or incorrect hold values.

**Independent Test**: Start a fade, pause MTC, verify no new OSC messages. Resume MTC at the same position, verify evaluation continues smoothly. Rewind MTC before `start_mtc_ms`, verify OSC output is `start_value`.

**Acceptance Scenarios**:

1. **Given** a running fade, **When** MTC stops, **Then** no further OSC messages are sent until MTC resumes.
2. **Given** MTC resumes at position `T` partway through a fade, **When** the first tick fires, **Then** the OSC value matches the curve evaluated at `clamp((T - start_mtc_ms) / duration_ms, 0, 1)`.
3. **Given** MTC rewinds to before `start_mtc_ms`, **When** a tick fires, **Then** the OSC message value equals `start_value`.

---

### Edge Cases

- What happens when `duration_ms = 0`? The fade immediately completes with `end_value` on the first tick.
- What happens when two fades with the same `fade_id` are registered? The second replaces the first (prior entry auto-cancelled without final OSC; `fade_error` with `reason: "superseded"` emitted).
- What happens when two fades with different `fade_id`s but the same `(host, port, path)` target are registered? The prior fade is auto-cancelled on arrival of the new one (FR-014 — same supersede rule as same-id collision).
- What happens when the status worker queue overflows? Oldest status is dropped with a warning log; the tick thread is never stalled.
- What happens when MTC timestamp goes backward due to SMPTE discontinuity? `t = clamp(...)` at 0 keeps the fade at `start_value` — no negative values, no crash.
- What happens if `lo_address` creation fails (bad host/port)? `addFade` fails with an error log and a `fade_error` status is enqueued; no `ActiveFade` is registered.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST evaluate all registered fades on every MTC quarter-frame tick, computing `t = clamp((mtc_ms - start_mtc_ms) / duration_ms, 0.0, 1.0)` and `value = start_value + (end_value - start_value) * curve->evaluate(t)`.
- **FR-002**: The system MUST send one UDP OSC message per active fade per tick with the computed float value to the fade's registered OSC host, port, and path.
- **FR-003**: The system MUST track `last_sent_value` on each active fade so cancel-with-hold and crash recovery have a known parameter state.
- **FR-004**: The system MUST mark a fade completed and remove it from the registry when `t >= 1.0`, sending one final OSC message with `end_value`.
- **FR-005**: The system MUST enqueue a `(fade_complete, fade_id)` status tuple when a fade completes (FR-004), without blocking the tick thread.
- **FR-006a**: The system MUST enqueue a `(fade_error, fade_id, "osc_send_failed")` tuple when consecutive OSC send failures on a single fade exceed a defined threshold.
- **FR-006b**: The system MUST NOT call NNG send directly from the MTC tick thread; all NNG output from the tick thread MUST route through a status worker queue.
- **FR-007**: `NngBusClient` MUST own a status worker thread with a bounded SPSC queue (fixed capacity, drop-oldest on overflow with warning log) that pops status tuples and emits NNG messages.
- **FR-008**: `FadeRegistry::cancelFade(fade_id, snap_to_end)` MUST send one final OSC message at `end_value` when `snap_to_end = true`, and send no additional OSC when `snap_to_end = false`.
- **FR-009**: `FadeRegistry::cancelAll()` MUST cancel all active fades without sending any final OSC values.
- **FR-010**: `OscSender` MUST provide a simple interface for sending a single float value to a host/port/path and returning the send result code.
- **FR-011**: Each active fade MUST store a pre-built OSC target address (created once at registration) to avoid address allocation on the real-time tick path.
- **FR-012**: `GradientEngine` MUST wire all three threads: NNG recv thread, MTC tick thread, and 100 ms fallback drain thread. The tick thread drains the command queue, applies commands to `FadeRegistry`, calls `FadeRegistry::tick(mtc_ms)`, and removes completed fades.
- **FR-013**: `ActiveFade` MUST include an optional crossfade partner pointer field (defaulting to null) so that Phase 7 can link paired entries without modifying the data model. Phase 4 does NOT implement `START_CROSSFADE` command handling, paired evaluation, or complementary-value output — those are deferred to Phase 7.
- **FR-014**: On `addFade`, the registry MUST detect any existing active fade whose `(osc_host, osc_port, osc_path)` tuple matches the incoming fade. Any such prior fade MUST be auto-cancelled without sending a final OSC value, and a `fade_error` status tuple with `reason: "superseded"` MUST be enqueued for the superseded fade. The new fade is then registered and begins ticking normally.
- **FR-015**: `Curve` construction (including `ResampledCurve` LUT population via `CurveFactory`) MUST occur inside `FadeRegistry::addFade` on the tick thread, not on the NNG recv thread. The NNG recv thread parses JSON into a `FadeCommand` struct (raw fields only) and enqueues it; the tick thread is the sole owner of `Curve` and `ActiveFade` allocations.
- **FR-016**: When `FadeCommand::start_mtc_ms == -1`, `FadeRegistry::addFade` MUST resolve the sentinel by reading `MtcTickSource::getMtcMs()` at command-apply time and storing that concrete value in `ActiveFade::start_mtc_ms`. The `-1` sentinel MUST NOT propagate into `ActiveFade` or into the tick evaluation formula.

### Key Entities

- **ActiveFade**: One running fade instance — holds `fade_id`, pre-resampled curve (built on the tick thread at `addFade`), pre-built OSC target address, `osc_path`, `start_value`, `end_value`, `start_mtc_ms`, `duration_ms`, `last_sent_value`, `completed` flag. Also carries an optional crossfade partner pointer (null in Phase 4; wired up in Phase 7).
- **FadeRegistry**: Map of `fade_id` to active fade. Owns `addFade`, `cancelFade`, `cancelAll`, `tick` methods. All methods called exclusively from the tick or fallback drain thread — no internal mutex required.
- **GradientEngine**: Top-level orchestrator. Owns and wires `MtcTickSource`, `NngBusClient`, and `FadeRegistry`. Sets the MTC quarter-frame callback to the tick function.
- **OscSender**: Thin UDP OSC wrapper. Stateless per-call interface. OSC target address lifetime is owned by `ActiveFade`.
- **StatusEmitRequest**: `(StatusKind, fade_id, reason)` tuple pushed by tick thread onto the status worker queue.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: OSC value at `t = 0.5` for any registered curve is within ±0.005 of the mathematically correct curve evaluation, verified by unit tests.
- **SC-002**: The tick thread is never blocked by NNG I/O — status worker queue overflow results in drop-oldest with log, never a stall.
- **SC-003**: A `fade_complete` NNG status message is emitted within two status worker drain cycles (worst case ~200 ms) after `t >= 1.0`.
- **SC-004**: `cancelAll()` completes and all OSC output ceases within one tick cycle (≤ 5 ms at 25 fps MTC) of the call.
- **SC-005**: Unit tests cover: curve value accuracy at t=0, t=0.5, t=1.0; completion detection; cancel snap vs hold; status queue drop-oldest. Crossfade paired-evaluation tests are deferred to Phase 7.
- **SC-006**: OSC send failure detection triggers a `fade_error` status after the consecutive-failure threshold is reached, with no hang or crash.
- **SC-007**: With 50 concurrent active fades on a 25 fps MTC source (200 Hz tick rate), the tick function completes within 2 ms wall-clock per tick (p99), leaving ≥3 ms headroom before the next tick. Measured in a loopback OSC benchmark.

## Assumptions

- Phases 1, 2, and 3 are implemented and available: `gme::gradient` (CurveFactory, ResampledCurve, CrossfadePair), `gme::time` (MtcTickSource with quarter-frame callback), `gme::signal` (FadeCommand, LockFreeQueue), and `NngBusClient` with `sendStatus` API and fallback drain thread.
- OSC targets are always on localhost — no network latency concern for OSC sends from the tick thread.
- liblo is available as a build dependency.
- The `FadeRegistry` is exclusively driven by the tick thread for evaluation; command application (add/cancel) is also done from the tick thread after draining the `LockFreeQueue`. No internal mutex is required.
- Consecutive OSC failure threshold defaults to 5 and is a compile-time constant in Phase 4 (not user-configurable).
- `START_CROSSFADE` command handling is out of scope for Phase 4. If a `START_CROSSFADE` `FadeCommand` reaches the tick thread in Phase 4, it is logged and dropped (no partner linkage, no paired evaluation). Full crossfade semantics are implemented in Phase 7.
- The status worker bounded queue capacity is 64 slots, matching the inbound `LockFreeQueue` capacity.
