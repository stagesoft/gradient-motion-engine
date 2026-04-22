# Feature Specification: Phase 3 — NNG Bus Client, FadeCommand & LockFreeQueue

**Feature Branch**: `005-nng-bus-client`  
**Created**: 2026-04-22  
**Status**: Draft  
**Input**: User description: "start Phase 3 development as layed out in dev/C++ Node-Level FadeEngine — Implementation Plan.md"

## Clarifications

### Session 2026-04-22

- Q: How does an individual `gradient-motiond` instance determine which incoming fade commands belong to it in a multi-node setup where NNG bus0 broadcasts to all peers? → A: Add a `node_name` field to the command JSON; the daemon reads its node name from config and filters on it.
- Q: What is the schema and send trigger for outgoing status messages from `gradient-motiond`? → A: Mirror the incoming NodeOperation envelope — `type: "status"`, `action: "update"`, `sender: "fadeengine_<node_name>"`, `target: "fade_complete"` or `"fade_error"`, with `data` carrying `fade_id`, `node_name`, and (for errors) a description. Emit `fade_complete` once per fade on completion; emit `fade_error` on OSC send failure or parse error tied to a known fade_id.
- Q: How is a crossfade encoded in a single NNG JSON message? → A: Flat fields in `data` with `partner_*` prefix for the second side, mirroring the FadeCommand struct exactly. Shared timing/curve fields (`duration_ms`, `curve_type`, `curve_params`, `osc_host`, `osc_port`, `start_mtc_ms`) apply to both sides; per-side fields (`osc_address`, `start_value`, `end_value`) appear once for the primary fade and again with `partner_` prefix for the paired fade. The B side's evaluated value is derived via `CrossfadePair` to guarantee complementary values.
- Q: What should `gradient-motiond` do when asked to shut down (SIGTERM/SIGINT) while fades are active? → A: Treat shutdown as `CANCEL_ALL` — stop evaluating, hold parameters at `last_sent_value` (do not send final OSC), emit one `fade_error` status per active fade with `reason: "daemon_shutdown"`, close the NNG socket cleanly, and exit within 2 seconds.
- Q: How should the daemon handle unknown JSON fields in a well-formed fade command message? → A: Silently ignore unknown top-level `data` fields and unknown `curve_params` keys; enforce strict presence and typing only on required fields. Missing or wrong-typed required fields are rejected (emit `fade_error` if `fade_id` is known, log locally otherwise).

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Fade Commands Received from Controller (Priority: P1)

The GradientEngine daemon receives fade commands dispatched by the Controller over the shared NNG bus. When the Controller instructs a node to start, cancel, or cancel all active fades, the daemon parses the message, validates it is addressed to `fadeengine`, and enqueues the structured command for the tick loop to process.

**Why this priority**: This is the primary integration point — without command ingestion the fade engine cannot function. All other phases depend on commands arriving correctly.

**Independent Test**: Can be tested by sending a well-formed JSON fade command to the NNG bus and verifying the daemon parses it into a correct FadeCommand record with all fields populated. No MTC or OSC is required.

**Acceptance Scenarios**:

1. **Given** the daemon is connected to the NNG bus, **When** the Controller sends a `start_fade` JSON message addressed to `target: "fadeengine"`, **Then** the daemon parses it into a FadeCommand with all fields (fade_id, osc_address, osc_port, osc_host, start_value, end_value, duration_ms, curve_type, curve_params, start_mtc_ms) correctly populated.
2. **Given** the daemon is connected, **When** a JSON message arrives with `target` set to any value other than `"fadeengine"`, **Then** the message is silently discarded and no FadeCommand is produced.
3. **Given** the daemon is connected and configured with node name `nodeA`, **When** a `start_fade` JSON message arrives with `target: "fadeengine"` but `data.node_name` set to `nodeB`, **Then** the message is silently discarded and no FadeCommand is produced.
4. **Given** the daemon is connected, **When** a `cancel_fade` message arrives with a `fade_id`, **Then** a CANCEL_FADE FadeCommand is enqueued with the correct fade_id.
5. **Given** the daemon is connected, **When** a `cancel_all` message arrives, **Then** a CANCEL_ALL FadeCommand is enqueued regardless of any fade_id field.
6. **Given** the daemon is connected, **When** a `start_crossfade` message arrives with both primary fields and `partner_*` fields populated, **Then** a single START_CROSSFADE FadeCommand is enqueued with all primary and partner fields intact and with shared timing/curve fields applied to both sides.

---

### User Story 2 — Thread-Safe Command Hand-off to Tick Loop (Priority: P1)

The NNG receive thread must hand off parsed FadeCommands to the MTC tick thread without blocking either thread. The queue must never stall the NNG thread — if capacity is exceeded, the oldest unread command is dropped and a warning is logged.

**Why this priority**: Incorrect cross-thread hand-off would cause data races or tick-loop stalls that corrupt fade timing. This is a correctness requirement for the entire engine.

**Independent Test**: Can be tested by filling the queue to capacity and verifying the oldest entry is overwritten (not blocked), and that a warning is emitted. Also verify that a producer and consumer running concurrently produce no data corruption across at least 10,000 push/pop cycles.

**Acceptance Scenarios**:

1. **Given** a fresh queue, **When** a FadeCommand is pushed by the NNG thread, **Then** the tick thread can pop the same command with all fields intact.
2. **Given** the queue is at maximum capacity (64 entries), **When** a new command is pushed, **Then** the oldest entry is dropped, the new entry is stored, and a warning is logged — the push call does not block.
3. **Given** simultaneous push and pop from two threads, **When** 10,000 commands are exchanged, **Then** no entries are duplicated, no entries are partially read, and no deadlock occurs.

---

### User Story 3 — Commands Processed When MTC Is Stopped (Priority: P2)

When MTC transport is stopped (no tick callbacks firing), fade commands — especially `CANCEL_ALL` sent during project unload — must still be drained and delivered to the fade registry. A low-frequency fallback timer drains the command queue independent of MTC ticks. It does not evaluate curves or send OSC; it only applies add/cancel operations.

**Why this priority**: Without this, a `CANCEL_ALL` command arriving during project stop would be silently lost, leaving stale fades active against dead player ports on the next project load.

**Independent Test**: Can be tested by pushing a CANCEL_ALL command while MTC is not running and verifying the queue is drained within 200ms without any MTC tick occurring.

**Acceptance Scenarios**:

1. **Given** MTC transport is stopped (no ticks firing), **When** a `CANCEL_ALL` command is pushed to the queue, **Then** the fallback timer drains it within 200ms and the fade registry receives the cancellation.
2. **Given** MTC transport is stopped, **When** a `start_fade` command arrives, **Then** it is enqueued and drained by the fallback timer (the fade will start evaluating once MTC resumes).
3. **Given** the fallback timer is active, **When** MTC transport is running, **Then** the fallback timer coexists without causing double-processing of commands (each command is consumed exactly once).

---

### Edge Cases

- What happens when a JSON message is malformed or missing required fields? — The message is rejected and no partial FadeCommand is enqueued; if the `fade_id` can be parsed, a `fade_error` status is emitted, otherwise a local warning is logged.
- What happens when a JSON message contains fields the daemon does not recognize? — Unknown top-level `data` fields and unknown `curve_params` keys are silently ignored; required fields are still validated normally.
- What happens if the NNG connection to the hub is refused at startup? — The client retries with exponential backoff (minimum 1s, maximum 30s) without blocking the main thread.
- What happens if the NNG socket disconnects mid-session? — Reconnect behavior follows the same backoff policy; commands received during the gap are lost (acceptable — the Controller will re-send on reconnect detection).
- What happens when `start_mtc_ms` is `-1`? — The FadeCommand is stored with `-1`; the engine will substitute the current MTC position when the command is applied.
- What happens when the same `fade_id` arrives twice as `start_fade`? — Treated as a replacement; the previous fade with that ID is cancelled before the new one is registered.
- What happens when the daemon receives SIGTERM while fades are active? — Active fades are cancelled in place (parameters held at `last_sent_value`, no final OSC sent), one `fade_error` status per active fade is emitted with `reason: "daemon_shutdown"`, the NNG socket is closed, and the daemon exits within 2 seconds.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The NNG client MUST connect to the hub bus using non-blocking dial with automatic reconnection on disconnect, without blocking the daemon startup.
- **FR-002**: The NNG client MUST receive messages in a dedicated background thread and not block the MTC tick thread.
- **FR-003**: The NNG client MUST filter incoming messages by target field — only messages where `target == "fadeengine"` are parsed into FadeCommands; all others are discarded.
- **FR-003a**: The NNG client MUST also filter by node identity — only messages where the `node_name` field in the `data` payload matches the daemon's own configured node name are processed; messages with a missing or mismatched `node_name` are silently discarded (they are intended for another node or are malformed). The daemon reads its node name at startup from configuration (same value that NodeEngine uses).
- **FR-004**: The NNG client MUST parse the JSON `data` payload of accepted messages into a fully populated FadeCommand, including all fade parameters and curve configuration.
- **FR-005**: The NNG client MUST support four command types: `start_fade`, `cancel_fade`, `cancel_all`, and `start_crossfade`.
- **FR-006**: The NNG client MUST be able to send status messages back to the bus for consumption by the Controller, using the same JSON envelope as incoming commands: `type: "status"`, `action: "update"`, `sender: "fadeengine_<node_name>"`, and `target` set to either `"fade_complete"` or `"fade_error"`. The `data` payload MUST include `fade_id` and `node_name`; `fade_error` messages MUST additionally include a human-readable `reason` field.
- **FR-006a**: The daemon MUST emit exactly one `fade_complete` status message per fade when the fade reaches its end time (t ≥ 1.0).
- **FR-006b**: The daemon MUST emit a `fade_error` status message when an active fade fails (for example: OSC send failure on its target endpoint, or a malformed-command parse error that can be attributed to a known `fade_id`). Errors that cannot be attributed to a specific `fade_id` are logged locally only.
- **FR-007**: The command queue MUST operate lock-free between the NNG receive thread (producer) and the MTC tick thread (consumer).
- **FR-008**: The command queue MUST have a fixed capacity of 64 entries and MUST drop the oldest entry (not block) when full, logging a warning on each drop.
- **FR-009**: A fallback drain mechanism MUST process the command queue at a low frequency (every 100ms) when MTC ticks are not firing, ensuring commands such as `CANCEL_ALL` are never silently lost.
- **FR-010**: The fallback drain MUST only apply add/cancel operations to the fade registry; it MUST NOT evaluate curves or send OSC messages.
- **FR-011**: A FadeCommand record MUST carry: command type, fade_id, osc_host, osc_port, osc_path, start_value, end_value, duration_ms, curve_type, curve_params, start_mtc_ms; and for crossfades: partner_fade_id, partner_osc_path, partner_start_value, partner_end_value.
- **FR-011a**: A `start_crossfade` JSON message MUST encode both fades in a single flat `data` payload using `partner_*` prefixed fields for the second side. The shared fields (`duration_ms`, `curve_type`, `curve_params`, `osc_host`, `osc_port`, `start_mtc_ms`, `node_name`) appear once and apply to both sides; the primary side carries `fade_id`, `osc_address`, `start_value`, `end_value`; the partner side carries `partner_fade_id`, `partner_osc_address`, `partner_start_value`, `partner_end_value`. A single FadeCommand of type `START_CROSSFADE` MUST be produced from this message.
- **FR-012**: The system MUST log a warning when a JSON message addressed to `fadeengine` cannot be parsed, including a description of the parsing failure.
- **FR-013**: On receiving SIGTERM or SIGINT, the daemon MUST perform a graceful shutdown sequence: (1) stop accepting new commands from the NNG receive thread; (2) apply `CANCEL_ALL` semantics internally — halt fade evaluation without sending any final OSC values (parameters remain at `last_sent_value`); (3) emit one `fade_error` status message per active fade with `reason: "daemon_shutdown"`; (4) close the NNG socket cleanly; (5) exit. The full shutdown sequence MUST complete within 2 seconds of the signal.
- **FR-014**: The JSON parser MUST silently ignore unknown top-level fields inside the `data` payload and unknown keys inside `curve_params`, to preserve forward compatibility with future Controller versions. All fields listed as required in FR-011 and FR-011a MUST be present and of the correct type; a message missing a required field (or carrying a wrong-typed required field) MUST be rejected — the daemon emits a `fade_error` status if the `fade_id` is parseable, otherwise logs a local warning only.

### Key Entities

- **FadeCommand**: A structured record representing a single instruction dispatched from the Controller to the fade engine. Carries all information needed to start, cancel, or crossfade — including timing origin, OSC target, value range, and curve configuration.
- **LockFreeQueue**: A fixed-capacity single-producer / single-consumer ring buffer that transfers FadeCommands from the NNG receive thread to the MTC tick thread. Overflow policy is drop-oldest to prevent producer stalls.
- **NngBusClient**: The daemon-side component responsible for connecting to the NNG bus, receiving and filtering messages, parsing them into FadeCommands, enqueuing them, and sending status responses.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A fade command sent from a test producer reaches the command queue within 5ms of being transmitted on the NNG bus under normal load.
- **SC-002**: The NNG receive thread never blocks the MTC tick thread — tick callback latency is unaffected (within measurement noise, ±1ms) whether or not NNG messages are arriving.
- **SC-003**: Under sustained message load of 100 commands/second, no more than 1 in 1000 messages addressed to `fadeengine` is lost due to queue overflow (queue capacity of 64 is sufficient for expected command rates).
- **SC-004**: A `CANCEL_ALL` command sent while MTC is stopped is delivered to the fade registry within 200ms via the fallback drain timer.
- **SC-005**: All fields of a well-formed `start_fade` JSON message are correctly round-tripped into the corresponding FadeCommand fields with no data loss — verified by the `test_nng_parse.cpp` unit test suite.
- **SC-006**: The NNG client reconnects automatically after a hub disconnect without requiring a daemon restart, within the configured backoff window (max 30s).
- **SC-007**: For every fade that runs to completion, exactly one `fade_complete` status message is observable on the bus within 50ms of the fade's end time; the message's `data.fade_id` matches the originating command's `fade_id`.
- **SC-008**: On SIGTERM with N active fades, the daemon emits exactly N `fade_error` status messages with `reason: "daemon_shutdown"` and exits cleanly within 2 seconds, verified by a systemd stop test.

## Assumptions

- The NNG hub is already running and accessible at `tcp://{controller_url}:{nng_hub_port}` (default port 9093) when the daemon starts.
- The Controller uses the existing NodeOperation JSON protocol with `type: "command"`, `action: "update"`, `sender: "controller"`, and `target: "fadeengine"` for all fade commands. The `data` payload carries a `node_name` field so each node's daemon can filter commands intended for its own node.
- Each node has a single, stable `node_name` available at daemon startup from the shared configuration source (the same identifier NodeEngine uses for itself on the NNG bus).
- Only one NNG client instance runs per daemon process; the design does not need to support multiple concurrent bus connections.
- The MTC tick thread is the sole consumer of the LockFreeQueue; no other thread pops from it.
- FadeCommand volume is low (a few commands per cue trigger, not per tick); the 64-entry queue capacity is sufficient and overflow in normal operation is an exceptional event.
- The `start_mtc_ms: -1` sentinel is used to mean "start at current MTC position" — the value `0` is a valid MTC position and must not be treated as a default.
- JSON parsing uses the nlohmann/json library already declared as a project dependency.
- The NNG client lives in `daemon/comms/` (daemon-specific); FadeCommand and LockFreeQueue live in `src/signal/` (library-reusable). The library has no dependency on the NNG client.
