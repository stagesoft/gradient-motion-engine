# Feature Specification: Phase 2 — MTC Tick Source

**Feature Branch**: `003-mtc-tick-source`  
**Created**: 2026-04-16  
**Status**: Draft  
**Input**: User description: "Start the implementation of Phase 2 as described in the implementation plan (MTC Tick Source / gme::time). New requirement: MtcReceiver::setNetworkMode(true) must be called when initializing the mtcreceiver implementation."

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Engine Receives Timecode-Driven Ticks (Priority: P1)

As a gradient engine process running on a CUEMS node, I need to receive a callback every MTC quarter-frame so that fade curves are evaluated in lock-step with the incoming MIDI timecode rather than on a free-running timer.

**Why this priority**: This is the core deliverable of Phase 2 — without quarter-frame callbacks the engine cannot evaluate curves at timecode positions. Everything in Phases 4 and 5 depends on this signal.

**Independent Test**: Can be fully tested by constructing a `MtcTickSource`, registering a callback, feeding synthesised MTC quarter-frame MIDI messages into the MIDI port, and verifying the callback fires at the expected positions with correct millisecond values.

**Acceptance Scenarios**:

1. **Given** `MtcTickSource` is started with a valid MIDI port and network mode enabled, **When** MTC quarter-frame messages arrive at 25 fps (200 Hz), **Then** the registered tick callback fires for every quarter frame with the current `mtcHead` millisecond value.
2. **Given** the tick callback is registered before `start()` is called, **When** MTC starts running, **Then** the callback begins firing immediately on the first quarter-frame decode without requiring any additional configuration.
3. **Given** `MtcTickSource` is running and MTC transport stops, **When** no further quarter-frame messages arrive, **Then** the callback ceases to fire and `isRunning()` returns `false`.

---

### User Story 2 — Timecode Position is Queryable at Any Time (Priority: P2)

As a component of the gradient engine (e.g., the tick thread or a fallback drain thread), I need to read the current MTC position in milliseconds at any moment so that I can evaluate curves against absolute timecode positions.

**Why this priority**: The MTC millisecond value is needed not only inside the callback but also for on-demand reads (e.g., when a new fade command arrives while MTC is running and must be anchored to the current position).

**Independent Test**: Can be tested by calling `getMtcMs()` while a synthetic MTC stream is running and verifying the returned value increases monotonically and matches the expected position within ±1 quarter-frame duration.

**Acceptance Scenarios**:

1. **Given** MTC is running at 25 fps, **When** `getMtcMs()` is called between quarter-frame ticks, **Then** it returns the last-committed MTC head position as a non-negative long value.
2. **Given** MTC has never started, **When** `getMtcMs()` is called, **Then** it returns 0 (the zero-initialised `mtcHead`).

---

### User Story 3 — Network Mode is Enabled for MTC Reception (Priority: P2)

As the gradient engine daemon, I need MTC reception to work in network mode so that MIDI timecode can be received over the network, matching the CUEMS node network topology.

**Why this priority**: This is a mandatory new requirement: `MtcReceiver::setNetworkMode(true)` must be called before opening the MIDI port. Without it the daemon will not receive MTC in a standard CUEMS network deployment.

**Independent Test**: Can be verified by inspecting that `setNetworkMode(true)` is called prior to `start()`; integration-tested by confirming the daemon receives MTC from a remote source on the network.

**Acceptance Scenarios**:

1. **Given** `MtcTickSource::start()` is called, **When** the MIDI port is opened, **Then** `MtcReceiver::setNetworkMode(true)` has already been invoked so the receiver is in network mode.
2. **Given** network mode is enabled, **When** MTC arrives over the network interface, **Then** quarter-frame callbacks fire exactly as they do for local MIDI sources.

---

### User Story 4 — Quarter-Frame Callback Fires from the Correct Decode Site (Priority: P3)

As a developer integrating the MTC extension, I need the quarter-frame callback to fire from inside `decodeQuarterFrame()` (not from `midiCallback()`), and specifically after BOTH `mtcHead` update sites, so that the callback always sees the freshest committed position.

**Why this priority**: Firing from the wrong site would cause the callback to see stale or partial positions, producing incorrect curve evaluations. This is a correctness constraint on the MtcReceiver extension.

**Independent Test**: Can be tested by verifying (via unit test or code inspection) that the callback is called after both update sites in `decodeQuarterFrame()`: the per-quarter running update and the per-complete-frame decode.

**Acceptance Scenarios**:

1. **Given** a complete MTC frame decode occurs (all 8 quarter frames received), **When** `decodeQuarterFrame()` processes the final quarter frame, **Then** the callback is invoked after `mtcHead` is updated to the full-frame millisecond value.
2. **Given** a partial (running) quarter-frame update occurs, **When** `decodeQuarterFrame()` performs the per-quarter increment, **Then** the callback fires after that increment so consumers see an updated (not stale) position.

---

### Edge Cases

- What happens when `start()` is called with an invalid or unavailable MIDI port name? `start()` returns a `MtcStartError` error code (e.g., `kPortNotFound`, `kNoPortsAvailable`); the caller (e.g., `GradientEngineApplication`) is responsible for checking the return value, logging, and aborting startup.
- What happens when `setTickCallback()` is called while MTC is already running? `setTickCallback()` is safe to call only before `start()`; calling it after MTC is running is undefined behavior. This is an accepted constraint matching the static-member design of `MtcReceiver`.
- What happens when `MtcReceiver`'s static members are used by two `MtcTickSource` instances in the same process? This is an accepted constraint (one instance per process); the limitation must be documented.
- What happens when `getMtcMs()` is called from multiple threads concurrently? The underlying `mtcHead` is a lock-free atomic, so concurrent reads are safe by design.
- How does the system handle MTC frame-rate changes mid-stream? `mtcHead` recalculates on each full-frame decode; `MtcTickSource` is not required to detect or report frame-rate changes.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide a `MtcTickSource` class in the `gme::time` namespace with `start(const std::string& midiPort)`, `getMtcMs()`, `isRunning()`, and `setTickCallback(std::function<void(long)>)` methods. `start()` MUST return a `MtcStartError` enum value indicating success (`kOk`) or the failure mode (`kNoPortsAvailable`, `kPortNotFound`). No exceptions cross the library boundary (Constitution: Performance & Safety Standards).
- **FR-002**: `MtcTickSource::start()` MUST call `MtcReceiver::setNetworkMode(true)` before opening the MIDI port to enable network MTC reception. `setNetworkMode()` already exists in `mtcreceiver` at commit `63ce3de`; no new addition to the upstream library is needed for this requirement.
- **FR-003**: The `MtcReceiver` class MUST be extended with a `static std::function<void(long)> onQuarterFrame` member that is invoked after BOTH `mtcHead` update sites inside `decodeQuarterFrame()`. The callback MUST NOT fire from `midiCallback()` or `decodeFullFrame()`.
- **FR-004**: `getMtcMs()` MUST return the value of `MtcReceiver::mtcHead.load()` — a lock-free atomic read, safe to call from any thread.
- **FR-005**: `isRunning()` MUST return the value of `MtcReceiver::isTimecodeRunning.load()` — a lock-free atomic read.
- **FR-006**: `setTickCallback()` MUST route the provided callback to `MtcReceiver::onQuarterFrame` so that the MtcReceiver extension and `MtcTickSource` share a single callback registration path.
- **FR-007**: The implementation MUST NOT introduce any free-running timer; all tick delivery is driven exclusively by incoming MTC quarter-frame MIDI messages.
- **FR-008**: The one-instance-per-process constraint (arising from `MtcReceiver`'s static members) MUST be documented in `MtcTickSource.h`.
- **FR-009**: `setTickCallback()` MUST be called only before `start()`; the implementation MAY assert or document this precondition but is not required to protect against concurrent callback replacement at runtime.

### Key Entities

- **MtcTickSource**: Adapter class in `gme::time` that wraps `MtcReceiver`'s statics behind an object-oriented interface, owns the MIDI port lifecycle, and routes the quarter-frame callback.
- **MtcReceiver (extended)**: Existing upstream class extended with `static std::function<void(long)> onQuarterFrame`; the callback is invoked inside `decodeQuarterFrame()` after each `mtcHead` update.
- **Quarter-frame tick**: The unit of timecode resolution — fires 8× per video frame (200 Hz at 25 fps, 240 Hz at 30 fps) carrying the current `mtcHead` millisecond value.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The tick callback fires at the correct rate — 8 times per MTC frame — for all supported frame rates (24, 25, 29.97 drop, 30 fps), with no missed or duplicated callbacks during a continuous 60-second MTC stream.
- **SC-002**: `getMtcMs()` returns a value within ±1 quarter-frame duration (≤ 5 ms at 25 fps) of the expected timecode position at any call site during a running MTC stream.
- **SC-003**: After `setNetworkMode(true)` is verified as called, the daemon successfully receives and processes MTC from a network source in a CUEMS node integration test without any additional configuration.
- **SC-004**: The existing unit tests for Phase 1 (`test_curves.cpp`) continue to pass unchanged after the Phase 2 changes are merged, confirming no regression.
- **SC-005**: A code-review check confirms the callback is placed after both `mtcHead` update sites in `decodeQuarterFrame()`, with no possibility of the callback seeing a stale value.

## Assumptions

- The `mtcreceiver` git submodule is already present and pinned to commit `63ce3de` (the RtMidi 6.x compatibility fix from `feat/rtmidi-api-rename`). After the upstream `master→main` branch rename, `63ce3de` is the HEAD of `main` — the submodule tracking reference must be updated accordingly (fetch + re-point to `origin/main`) but the pinned commit itself does not change at this step.
- `MtcReceiver::setNetworkMode(bool)` already exists at commit `63ce3de` — it is not a new addition for this phase.
- The `onQuarterFrame` extension MUST be committed to the upstream `stagesoft/mtcreceiver` repository (not only to the local submodule checkout) as an independent task; once merged/pushed to `main`, the submodule pin in `gradient-motion-engine` is advanced to the new commit.
- Consuming components (VideoComposer ~60 fps, AudioMixer at JACK period rate) operate at lower rates than the 200–240 Hz tick rate, so tick delivery at quarter-frame resolution is safe without throttling.
- A single `MtcTickSource` instance per process is an acceptable constraint; multi-listener support is explicitly out of scope for this phase.

## Clarifications

### Session 2026-04-16

- Q: What is the target state for the `mtcreceiver` submodule pin after this phase? → A: Commit `63ce3de` is already the HEAD of `main` after the upstream `master→main` rename; the submodule update task only needs to fetch and fix the local tracking branch. The `onQuarterFrame` extension is handled as a separate upstream commit; the submodule pin is then advanced to that new commit.
- Q: How should `MtcTickSource::start()` report a failure to open the MIDI port? → A: Return a `MtcStartError` enum value (`kNoPortsAvailable`, `kPortNotFound`); the application layer checks the return value, logs, and aborts startup. No exceptions cross the library boundary per Constitution Performance & Safety Standards.
- Q: What is the thread-safety contract for `setTickCallback()`? → A: Initialization-time only (before `start()`); concurrent replacement during live MTC is undefined behavior — accepted constraint, no mutex needed.
