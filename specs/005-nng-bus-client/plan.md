# Implementation Plan: Phase 3 — NNG Bus Client, FadeCommand & LockFreeQueue

**Branch**: `005-nng-bus-client` | **Date**: 2026-04-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/005-nng-bus-client/spec.md`

## Summary

Phase 3 adds the NNG ingest pipeline and the library-side signal transport it
feeds into: `FadeCommand` (library, `gme::signal`), `LockFreeQueue`
(library, `gme::signal`), and `NngBusClient` (daemon, `daemon/comms/`). The
daemon dials the CUEMS NNG bus as a `bus0` client, filters inbound messages to
those addressed to `target == "gradientengine"` AND a matching `node_name`, parses
the four supported commands (`start_fade`, `cancel_fade`, `cancel_all`,
`start_crossfade`) into a `FadeCommand` record, and hands them to the
MTC-tick thread via a fixed-capacity SPSC ring buffer with drop-oldest
overflow. A 100 ms fallback drain thread handles queue consumption when MTC
ticks are paused. The `NngBusClient::sendStatus` API is delivered and exercised
for parse-error `fade_error` emission (the only in-Phase-3 caller).

This phase does not start fades, evaluate curves, emit `fade_complete`, or
install a SIGTERM handler — those land in Phase 4 (fade evaluation and
`fade_complete`/OSC-failure `fade_error`) and Phase 5 (daemon signal handler
+ per-fade `fade_error` on shutdown). See `spec.md` §Scope Boundaries for
the full list. The contracts delivered here are the data structures
(`FadeCommand`, the queue) and the producer (`NngBusClient`); the consumer
in Phase 4 will own `FadeRegistry::apply(FadeCommand&)`.

## Technical Context

**Language/Version**: C++17 (GCC, `-Wall -O3 -pthread`)
**Primary Dependencies**: NNG 1.10.1 (`libnng-dev`, C API — `nng_bus0_open`,
`nng_dial`, `nng_recv`, `nng_send`); nlohmann/json 3.11.3 (header-only, JSON
parse + dump); standard library (`<atomic>`, `<array>`, `<thread>`,
`<chrono>`, `<string>`, `<functional>`, `<optional>`) for the library-side
queue and command types.
**Storage**: N/A — all state is in-memory. The queue is a fixed-size array;
no persistence.
**Testing**: CTest + hand-rolled `CHECK` macros (matching the project style
established in `tests/test_curves.cpp` and `tests/test_mtc_tick_source.cpp`).
New test files: `tests/test_nng_parse.cpp` (pure JSON → FadeCommand parsing,
no sockets) and `tests/test_lockfree_queue.cpp` (single-thread correctness,
SPSC stress with `std::thread`, drop-oldest policy). TSan build is expected
to exercise the SPSC test (the repo already carries a `build-tsan/` tree).
**Target Platform**: Linux (Debian/Ubuntu, GCC). Daemon runs under systemd;
library is portable C++17.
**Project Type**: C++ library (`libgradient_motion`) + thin daemon
(`gradient-motiond`) consumer. Phase 3 adds two library source files
(`FadeCommand.{h,cpp}` is header-only; `LockFreeQueue.h` is a template) and
one daemon subsystem (`daemon/comms/NngBusClient.{h,cpp}`).
**Performance Goals**: SC-001 — NNG-to-queue latency ≤ 5 ms under the SC-003
load (in-phase; verified in `tests/test_nng_integration.cpp`). SC-002 — NNG
receive thread never blocks the MTC tick thread (tick latency drift ≤ ±1 ms);
design-enforced in Phase 3, measurement deferred to Phase 4 when the tick
thread exists. SC-003 — under 100 cmd/s sustained load for ≥ 10 s, drop rate
≤ 1/1000 (in-phase; integration test). SC-004 — CANCEL_ALL observable at the
drain callback within 200 ms while MTC is stopped (in-phase; `test_lockfree_queue.cpp`).
SC-006 — reconnect within 30 s (in-phase; integration test). SC-007
(`fade_complete` within 50 ms) and SC-008 (N `fade_error` on SIGTERM, exit
≤ 2 s) are deferred to Phase 4 and Phase 5 respectively per spec.md §Scope
Boundaries.
**Constraints**: **Real-Time Safety (Principle IV)** — the tick-thread
consumer path (`LockFreeQueue::pop`) MUST be lock-free and
allocation-free. The producer path (`push` from the NNG thread) is allowed
one atomic CAS and zero allocations after construction. JSON parsing
happens *only* on the NNG receive thread, never on the tick thread. The
NNG socket runs in non-blocking dial mode (`NNG_FLAG_NONBLOCK`) with
reconnect min=1 s, max=30 s (matching pynng's `HubServices.py` line 217).
No exceptions cross the library boundary (NNG and nlohmann errors must be
converted to error codes or status bool). Forward-compat: unknown fields in
`data` and `curve_params` MUST be ignored (FR-014).
**Scale/Scope**: One `NngBusClient` per daemon process. One queue instance
per daemon (producer = NNG thread, consumer = tick thread; fallback drain
is the *same* consumer role, just scheduled by a timer instead of the MTC
callback — see research.md Decision 4). Expected command rate 1–10/s in
normal operation; queue sized for 64 to absorb burst/restart scenarios.
Three new source files + two new tests. No changes to gradient, motion,
engine, or time modules.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. Deterministic Evaluation | ✅ PASS | `FadeCommand` parsing is a pure function of the incoming JSON bytes + daemon config (`node_name`). The queue is FIFO with a deterministic drop-oldest policy keyed on capacity. No wall-clock reads, no randomness, no environment reads during evaluation. Transport jitter is confined to `NngBusClient` — it never feeds back into the evaluation layer. |
| II. Modular Architecture | ✅ PASS | Three new units, each with a single responsibility: `gme::signal::FadeCommand` (pure data), `gme::signal::LockFreeQueue` (SPSC ring), `NngBusClient` (daemon transport). No cyclical dependencies: `signal` depends only on `<atomic>` and `nlohmann/json`; `NngBusClient` depends on `signal` plus `libnng`. `gme::engine`, `gme::gradient`, `gme::time`, `gme::osc`, `gme::motion` are untouched. |
| III. Library-First | ✅ PASS | `FadeCommand` and `LockFreeQueue` live in `libgradient_motion` (reusable by tests, future embedding, and Phase 4's `FadeRegistry`). `NngBusClient` is daemon-only (protocol binding — correctly outside the library per Principle V). A future library consumer can build its own transport that produces `FadeCommand`s without pulling in NNG. |
| IV. Real-Time Safety | ✅ PASS | `LockFreeQueue::pop` is wait-free (single atomic load + indexed array read + atomic store of the read index). Zero heap allocation after construction. Drop-oldest on the producer side uses a single CAS — no blocking. JSON parsing and NNG I/O are strictly on the NNG receive thread, never on the MTC tick thread. Queue capacity is a compile-time constant (64); storage is an in-class `std::array<FadeCommand, 64>`. |
| V. Protocol-Agnostic Core | ✅ PASS | `NngBusClient` is the only component that knows about NNG and the NodeOperation JSON envelope; it lives in `daemon/comms/`, not in the library. `FadeCommand` carries only the evaluation-relevant data (OSC target, curve, timing) — it has no knowledge of NNG or JSON. The library can be linked into a non-NNG host (e.g., a test harness) and still consume `FadeCommand`s. |
| VI. Documentation Standards | ✅ PASS (action required) | All new public APIs (`FadeCommand`, `LockFreeQueue<T, N>`, `NngBusClient`) must ship with full Doxygen docstrings: brief + parameters + long description + errors + example. The `-DDOXYGEN_WARN_AS_ERROR=YES` CI gate will enforce this. Task list will explicitly include a docstring-audit task before merge. |

**Gate verdict**: PASS. No complexity deviations to justify.

## Project Structure

### Documentation (this feature)

```text
specs/005-nng-bus-client/
├── plan.md                     # This file (/speckit.plan command output)
├── research.md                 # Phase 0 output — design decisions
├── data-model.md               # Phase 1 output — entities, invariants, transitions
├── quickstart.md               # Phase 1 output — end-to-end walkthrough
├── contracts/                  # Phase 1 output — stable header contracts
│   ├── FadeCommand.h           # Library public header (contract)
│   ├── LockFreeQueue.h         # Library public header (contract)
│   └── NngBusClient.h          # Daemon subsystem header (contract)
├── checklists/
│   └── requirements.md         # Spec quality checklist (already present)
├── spec.md                     # Feature specification
└── tasks.md                    # Phase 2 output (/speckit.tasks — NOT created here)
```

### Source Code (repository root)

```text
src/
├── signal/
│   ├── FadeCommand.h           # NEW — command record + enum + JSON parse helper
│   ├── LockFreeQueue.h         # NEW — template SPSC ring, drop-oldest on full
│   └── signal.cpp              # Phase 0 placeholder → kept as a translation-unit anchor
├── time/                       # (untouched)
├── gradient/                   # (untouched)
├── engine/                     # (untouched)
├── osc/                        # (untouched)
└── motion/                     # (untouched)

daemon/
├── comms/                      # NEW directory
│   ├── NngBusClient.h          # NEW — dialer + receive thread + send API
│   └── NngBusClient.cpp        # NEW — NNG C API wiring + JSON envelope build/parse
├── config/                     # (existing — may add a getNodeName() accessor)
│   ├── ConfigurationManager.h  # MINOR — add --node-name CLI flag + getter
│   └── ConfigurationManager.cpp
├── GradientEngineApplication.h # (touched only if the owner wires NngBusClient here — see research Decision 7)
├── GradientEngineApplication.cpp
├── main.cpp                    # (untouched)
└── logging.h                   # (untouched — NngBusClient uses GME_LOG_* macros)

tests/
├── CMakeLists.txt              # NEW entries for test_nng_parse, test_lockfree_queue
├── test_nng_parse.cpp          # NEW — JSON → FadeCommand, round-trip, filter rules
├── test_lockfree_queue.cpp     # NEW — SPSC correctness, drop-oldest, TSan clean
├── test_curves.cpp             # (untouched)
└── test_mtc_tick_source.cpp    # (untouched)

CMakeLists.txt                  # MINOR — link libnng to gradient-motiond; move NNG find_package from OPTIONAL to REQUIRED gated by BUILD_DAEMON
src/CMakeLists.txt              # MINOR — add signal/FadeCommand.h, signal/LockFreeQueue.h to target_sources (headers only — they contribute install rules and Doxygen coverage)
```

**Structure Decision**: Existing single-project C++ layout with library
(`src/`) + daemon (`daemon/`) split established in spec 001 continues
unchanged. Phase 3 adds one new directory (`daemon/comms/`), two new
library headers (both in `src/signal/`), and two new test files. The
placeholder `src/signal/signal.cpp` is retained (matches the established
per-module anchor pattern — see `engine.cpp`, `motion.cpp`, `osc.cpp`) so
the `gradient_motion` static library continues to list one translation
unit per module.

## Complexity Tracking

No constitution gates fail. No deviations to justify.
