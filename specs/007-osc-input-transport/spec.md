# Feature Specification: Phase H — OSC Input Transport (replaces NNG)

**Feature Branch**: `feat/osc-input-transport`
**Created**: 2026-05-13
**Status**: Draft
**Supersedes**: [005-nng-bus-client](../005-nng-bus-client/spec.md) (inbound command transport only)
**Input**: User description: "use specs/planning/phase-h-osc-refactor-plan.md and its general summary specs/planning/PHASE-H-HANDOFF-NOTES.md to refactor nng input communication and use osc instead. Mark spec 005 as superseded by the new OSC spec."

## Background

Phase E integration testing on node-002 uncovered that the daemon never receives `start_fade` messages from the NodeEngine in a real CUEMS deployment, even after every wire-format mismatch on the Python side was fixed and `controller.local` resolved correctly. The root cause is architectural: NNG bus0 does **not** auto-relay between dialers in the star topology that CUEMS actually deploys, and the daemon was placed on the wrong CUEMS message plane.

CUEMS has two distinct planes:

| Plane | Carrier | Members | Direction |
|---|---|---|---|
| **Plane 1 — inter-node command bus** | NNG bus0 | Controller, NodeEngine | Cross-machine |
| **Plane 2 — local player control** | UDP OSC on localhost | AudioPlayer, VideoComposer, DmxPlayer | Same-machine |

`gradient-motiond` is functionally a Plane-2 player (runs on each node, takes commands from the local NodeEngine, drives an OSC sink) but was implemented as a Plane-1 NNG peer. This feature realigns it with the rest of the Plane-2 players.

This feature changes only the **input** transport into the daemon. The motion engine (FadeMotion, MotionRegistry, MotionFactory, curves, MtcTickSource) and the daemon's **output** transport (OSC sender to `/volmaster`, `/opacity`, etc.) are unchanged.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — A node-local fade plays end-to-end on real hardware (Priority: P1)

A CUEMS operator loads a project with an AudioCue and a FadeCue on node-002, presses GO, and hears the audio fade smoothly to silence over the configured duration. The fade is driven by the NodeEngine on that same node dispatching a fade command to the gradient daemon over localhost UDP. There is no controller relay, no `controller.local` resolution, and no NNG bus involvement on the command path.

**Why this priority**: This is the user-observable success criterion that motivated the refactor. The exact scenario currently fails in production (the daemon never receives the command). Restoring it is the whole point of Phase H. Without this story the feature delivers no value.

**Independent Test**: Load `/opt/cuems_library/projects/fade-test/` (AudioCue + FadeCue, target 0%, 5 s, linear) on node-002, press GO, and verify the audio fades smoothly to silence over 5 seconds with no audible bounce-back. Capture the daemon journal during the fade to confirm command receipt and per-tick OSC sends to `/volmaster`.

**Acceptance Scenarios**:

1. **Given** the daemon is running on node-002 and the local NodeEngine is healthy, **When** the operator presses GO on a FadeCue targeting a local AudioCue, **Then** the daemon receives the fade command on its local OSC port within 1 ms and begins evaluating the fade at the scheduled MTC start time.
2. **Given** a fade is being evaluated by the daemon, **When** the MTC tick fires, **Then** the daemon emits one OSC message to the target audio player path with a value interpolated by the configured curve, and the operator hears the volume change smoothly.
3. **Given** node-002 has no working `controller.local` hostname (Avahi misconfigured), **When** the operator runs the same fade-test project, **Then** the fade still works correctly — the daemon does not depend on any hostname resolution.

---

### User Story 2 — Stopping or reloading a project clears in-flight fades (Priority: P1)

When the operator presses STOP on the running script, or loads a different project, any fades currently being evaluated by the daemon are cancelled immediately and the target parameter is held at its last-sent value. No further OSC ticks are emitted for the cancelled fades.

**Why this priority**: Cancellation correctness is part of the existing contract (Phase 6 supersede / cancel semantics). The transport change must not regress it. STOP without cancellation leaves audio fading toward silence after the operator has explicitly halted the show — unacceptable.

**Independent Test**: With a 30-second fade in flight, press STOP. Verify all in-flight fades stop within one tick interval and no further OSC packets are emitted to their target paths.

**Acceptance Scenarios**:

1. **Given** one or more fades are active in the daemon's registry, **When** the operator presses STOP, **Then** the NodeEngine sends a cancel-all command over OSC and the daemon clears its registry within one MTC tick.
2. **Given** a fade with a known `motion_id` is active, **When** the controller chooses to cancel just that fade, **Then** the daemon receives a single-motion cancel and removes only that motion from the registry, leaving any other active fades untouched.
3. **Given** the operator loads a new project while fades from the previous project are still in flight, **When** the load completes, **Then** all previous-project fades have been cancelled before the new project's cues arm.

---

### User Story 3 — Multi-node deployments do not cross-talk (Priority: P2)

In a CUEMS deployment with multiple nodes, a fade scheduled on node-A is evaluated only by node-A's daemon. Node-B's daemon does not see, parse, or filter that command. Each node's daemon only handles its own work.

**Why this priority**: Today's broadcast-and-filter design has every daemon parse every fade just to drop the ones not addressed to it; with the new transport this becomes structurally impossible. The behavior matters for correctness on installations with more than one node, but the single-node case (which is the most common) is already covered by P1.

**Independent Test**: On a two-node test rig, dispatch a fade scheduled for node-A. Capture journal output on both daemons. Node-A logs receipt and evaluation; node-B logs nothing related to the fade.

**Acceptance Scenarios**:

1. **Given** node-A and node-B both run gradient daemons, **When** NodeEngine on node-A dispatches a local fade, **Then** node-B's daemon receives no traffic for that fade.
2. **Given** a multi-node deployment, **When** node-A's NodeEngine sends cancel-all on STOP, **Then** node-B's fades are not affected.

---

### User Story 4 — Operators and developers can observe fade activity without a wire receiver (Priority: P3)

When something goes wrong with a fade — a malformed command, a curve parameter parse failure, an OSC sink that cannot be reached — the operator or developer can see what happened by reading the daemon's journal (`journalctl -u cuems-gradient-motiond`). The daemon does not emit status broadcasts back over the network because no other CUEMS component consumes them today; fade lifecycle (start/end/progress) is the responsibility of the NodeEngine's cue loop, matching the pattern used by audio, video, and DMX players.

**Why this priority**: This is an operability concern, not a functional one. The capability that's being removed (status broadcasts to controller/frontend) was already dead code with no consumer, so removing it has no user-visible regression. The replacement (journal-based observability) is consistent with the other Plane-2 players.

**Independent Test**: Send a malformed fade command to the daemon's OSC port and verify the daemon logs a parse error to its journal without emitting any network status message and without crashing.

**Acceptance Scenarios**:

1. **Given** the daemon receives a fade command with a missing required field, **When** parsing fails, **Then** an error is logged to the daemon journal with the command name and the missing field, and the daemon continues running.
2. **Given** the daemon attempts to send an OSC tick to a downstream player port that is closed, **When** the send fails, **Then** the failure is logged to the journal once per affected fade and subsequent ticks continue to be attempted (the daemon does not abort the fade on a single failed send).

### Edge Cases

- **Late command arrival**: a fade command arrives after its scheduled `start_mtc_ms` has already passed. Daemon behavior matches existing Phase 6 semantics — the fade either starts immediately at its already-elapsed position or is rejected, whichever the existing registry implementation does today. (This refactor does not change tick-loop behavior.)
- **Duplicate motion_id**: a fade command arrives with the same `motion_id` as one already active. Existing supersede semantics apply; the new command replaces the in-flight one.
- **Cancel for unknown motion_id**: a cancel for an unknown motion is silently ignored.
- **Command for a different node**: the `node_name` field does not match the daemon's configured node name. The command is dropped silently — the previous broadcast design required this filter; with localhost UDP it becomes a defense-in-depth check.
- **Malformed command**: missing required field, wrong type, unparseable curve parameters. The command is rejected, a journal warning is emitted, and the daemon continues running.
- **OSC port already bound**: a second daemon instance, or another process holding the configured port, prevents the daemon from binding at startup. The daemon exits with a fatal log message naming the port and the conflict.
- **Daemon shutdown (SIGTERM/SIGINT)** with active fades: existing Phase 3 behavior is preserved — fades are treated as cancelled, target parameters are held at their last-sent value (no final OSC), and the daemon exits cleanly within 2 seconds. Status broadcasts that previously accompanied this are removed.

## Requirements *(mandatory)*

### Functional Requirements

**Daemon — command intake**

- **FR-001**: The daemon MUST accept fade commands over UDP OSC on a configurable local port (default 7100).
- **FR-002**: The daemon MUST listen only on the localhost interface — it MUST NOT bind to a public network interface.
- **FR-003**: The daemon MUST support at minimum the following OSC command set: start a fade, cancel a single fade by `motion_id`, cancel all active fades.
- **FR-004**: The daemon MUST drop commands whose `node_name` field does not match its configured node name, as a defense-in-depth filter against misrouted traffic.
- **FR-005**: The daemon MUST deduplicate active fades by `motion_id` using the existing supersede semantics from Phase 6 — a new `start_fade` with the same `motion_id` as an active fade replaces the active fade.
- **FR-006**: The daemon MUST validate required fields and types on every incoming command, reject malformed commands without crashing, and log the rejection reason to its journal.
- **FR-007**: The daemon MUST start successfully and bind its OSC port without requiring mDNS / Avahi name resolution or any controller hostname to be reachable.
- **FR-008**: The daemon MUST NOT emit any status messages over the network. Fade lifecycle observation is the NodeEngine cue loop's responsibility (matching the AudioPlayer/VideoComposer/DmxPlayer pattern).

**Daemon — internal handoff and behaviour preserved from earlier phases**

- **FR-009**: The daemon MUST hand received commands from its network-callback thread to its tick-evaluation thread in a way that does not block the network thread and does not introduce locks in the tick path. (The existing lock-free queue used for this purpose in Phase 6 satisfies this requirement.)
- **FR-010**: The daemon MUST preserve all motion, registry, and curve behavior previously specified in features 002 (curves), 003/004 (MTC tick source), and 006 (registry / tick loop). Only the command-intake path changes.
- **FR-011**: The daemon MUST preserve the SIGTERM/SIGINT shutdown contract — cancel all active fades, hold target parameters at their last-sent value, do not emit a final OSC tick, exit within 2 seconds.

**NodeEngine (cuems-engine) — command dispatch**

- **FR-012**: The NodeEngine MUST dispatch fade commands directly to the gradient daemon on the local node via UDP OSC, without routing through the controller or any NNG bus.
- **FR-013**: The NodeEngine MUST cancel all of the daemon's active fades when the running script is stopped and when a different project is loaded.
- **FR-014**: The NodeEngine MUST not require the daemon to be reachable for project load or script execution to succeed — i.e., daemon-side failures (port closed, daemon crashed) MUST NOT block other cue types from playing.
- **FR-015**: The NodeEngine MUST own fade lifecycle reporting. When a fade-progress UI surface is added in the future, the emission MUST come from the NodeEngine's fade cue loop, consistent with how audio, video, and DMX cue progress is reported.

**Deployment & packaging**

- **FR-016**: The daemon binary MUST NOT have a runtime dependency on libnng. The system package MUST NOT declare libnng in its runtime dependencies.
- **FR-017**: The systemd unit for the daemon MUST NOT declare a dependency on the Avahi daemon. Daemon startup MUST succeed even when Avahi is stopped or misconfigured.
- **FR-018**: The OSC port the daemon listens on MUST be configurable so a different node-local default can be selected if 7100 conflicts with another local service.

**Documentation & spec hygiene**

- **FR-019**: The existing NNG bus client feature spec (`005-nng-bus-client`) MUST be marked as superseded by this feature for the inbound command transport, with a header pointer from the old spec to this one. Sections of feature 005 unrelated to inbound transport (the FadeCommand struct definition, the lock-free queue, the node_name filter pattern) remain referenced by features 002/003/004/006 and are not invalidated.

**Out of scope for this feature** (intentionally excluded)

- Cross-machine fade dispatch (e.g., NodeEngine on node-A directly addressing daemon on node-B). All fades are assumed local to a single node, matching today's behavior.
- Status emission from the daemon to the controller, frontend, or any other consumer. No CUEMS component consumes those messages today; the future fade-progress UI feature lands in the NodeEngine fade cue loop, not the daemon.
- Phase 7 crossfade dispatch over the new transport. The command shape is compatible (a single OSC message can carry a crossfade pair) but emit-side work is a separate phase.
- Fixing Avahi misconfiguration on node-002 for non-daemon CUEMS components. This feature removes the daemon's dependency on Avahi; engine and editor still rely on it for inter-node discovery and that is a separate task.

### Key Entities

- **Fade command (start)**: a single instruction telling the daemon to begin evaluating a fade. Carries the motion identifier, the target node name, the OSC destination of the player to drive (host, port, path), the start and end values, the scheduled MTC start time, the duration, and the curve type with its parameters. Replaces the JSON envelope previously delivered over NNG.
- **Cancel command**: targets a single motion by `motion_id` (cancel one) or all motions (cancel all). Carries the target node name for defense-in-depth filtering.
- **OSC server (daemon side)**: the local-port UDP listener that receives commands from the local NodeEngine, parses them into in-memory command records, and hands them to the tick-evaluation path. Replaces the NNG bus client.
- **Gradient player (NodeEngine side)**: the new client wrapper used by NodeEngine to send commands to the local daemon, modeled after the existing video and DMX player wrappers. Replaces the NNG send paths in `NodeCommunications` and the cancel-all wrapper in `ControllerEngine`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The Phase E failing scenario passes — pressing GO on the `fade-test` project on node-002 produces an audible 5-second fade to silence with no bounce-back, with zero NNG-related warnings and zero `controller.local` references in the daemon journal during the fade.
- **SC-002**: A fade command dispatched by the local NodeEngine is received by the daemon in under 1 millisecond on the same host, measured end-to-end from send call to parse completion.
- **SC-003**: Daemon startup on node-002 succeeds with Avahi stopped, demonstrating no remaining dependency on hostname resolution for the gradient transport.
- **SC-004**: The daemon's installed binary has no dynamic-link dependency on libnng, and the daemon's system package declares no libnng runtime dependency.
- **SC-005**: On a multi-node test rig, a fade scheduled for one node produces no observable network or log activity related to that fade on any other node's daemon.
- **SC-006**: All previously green C++ and Python unit tests for motion logic, registry, curves, and MTC tick remain green after the transport change. The new transport gains its own test coverage for command parsing, malformed-command rejection, node-name filtering, and the network-to-tick handoff.
- **SC-007**: The Spec Kit feature 005 spec carries a visible "Superseded by" header pointing to this feature for the inbound transport, and the new daemon retains no live code path that opens an NNG socket.
- **SC-008**: The daemon source shrinks meaningfully when the NNG client and status-emission paths are deleted (approximately 600 lines per the planning analysis); the resulting code follows the same listener pattern used by `cuems-audioplayer` / `cuems-videocomposer` / `cuems-dmxplayer`.

## Assumptions

- The daemon and the NodeEngine that dispatches its commands always run on the same physical host (single-node-scope fades). This matches the current production design, where the fade cue and its target player cue are assumed colocated.
- The NodeEngine knows, or can be told via configuration, the local OSC port the daemon is listening on. The chosen mechanism (entry in `settings.xml` under a node-local key, with a built-in default) mirrors how other player ports are already configured.
- The default OSC port for the gradient daemon (7100) does not collide with any other CUEMS player on the same host. This is verified by reading the existing port assignments documented in the planning notes (videocomposer uses 7000, dmxplayer per-config); the default is configurable if a future player needs 7100.
- The receiving-side OSC implementation in the daemon (`oscpack` per the planning notes — though no implementation choice is mandated by this spec) supports the same `node_name` filter, motion_id deduplication, and curve-parameter handling that the old JSON path supported.
- The fade-progress UI feature, when built, will emit progress from the NodeEngine's fade cue loop the same way audio and video cue progress is emitted. This feature does not preempt that work and does not need to coordinate with it.
- The Debian packaging skeleton produced in Phase D is rebuilt against the new daemon binary; no other packaging changes are required beyond dropping `libnng-dev` from the build dependencies and adding the chosen OSC library.
