<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Architecture

This page documents the static structure of `gradient-motion-engine`: which components exist, what each owns, and how they depend on each other. For the live signal flow and the design rationale, see [index.md](index.md).

---

## Component graph

```
┌────────────────────────────────────────────────────────────────────────────┐
│                       gradient-motiond (daemon binary)                     │
│                                                                            │
│   GradientEngineApplication ─── ConfigurationManager                       │
│         │                                                                  │
│         │ owns                                                             │
│         ▼                                                                  │
│   gme::engine::GradientEngine                                              │
│         │                                                                  │
│         ├─ owns ── gme::time::MtcTickSource ───────► mtcreceiver v2        │
│         │                                            (RtMidi)              │
│         │                                                                  │
│         ├─ owns ── gme::signal::LockFreeQueue<FadeCommand, 64>             │
│         │                                                                  │
│         ├─ owns ── gme::daemon::comms::OscServer ──► liblo (UDP)           │
│         │           │                                                      │
│         │           └─ uses ─ gme::signal::parseFadeOscCommand             │
│         │                                                                  │
│         └─ owns ── gme::motion::MotionRegistry                             │
│                    │                                                       │
│                    ├─ holds ─ std::unordered_map<id, IMotion>              │
│                    │                                                       │
│                    └─ uses ── gme::motion::MotionFactory                   │
│                                │                                           │
│                                ├─ uses ── gme::gradient::CurveFactory      │
│                                │           (LinearCurve, SigmoidCurve,     │
│                                │            BezierCurve, EaseIn/OutCurve,  │
│                                │            SCurve, all wrapped in         │
│                                │            ResampledCurve)                │
│                                │                                           │
│                                └─ produces ── gme::motion::FadeMotion      │
│                                                │                           │
│                                                └─ uses ─ gme::osc::sendFloat│
│                                                          (liblo UDP)       │
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘
```

The daemon (`gradient-motiond`) is the only binary. Tests link `gradient_motion` (the static library) plus a per-test set of mocks and helpers. The library itself does not link liblo for consumers that don't use `OscServer` directly — `OscServer` is forward-declared in `GradientEngine.h` and `GradientEngine.cpp` is compiled into the daemon target.

---

## Module dependency direction

```
gme::engine          (depends on)
   │
   ├──► gme::motion           ──► gme::gradient
   │      │                          │
   │      └──► gme::signal           └──► (no further GME deps)
   │             │
   │             └──► (nlohmann_json, liblo for argv types)
   │
   ├──► gme::time             ──► mtcreceiver
   │
   └──► gme::osc              ──► liblo
```

Rules:

- **No cycles.** `gme::gradient` depends on nothing else in GME; `gme::time` depends on nothing else in GME; `gme::osc` depends on nothing else in GME.
- **`gme::motion` is the integrator** of `gradient` + `osc` + `signal`. It does not depend on `time` directly — the registry receives `mtc_ms` as a `long` from the engine layer.
- **`gme::engine` wires everything.** It is the only namespace that depends on the daemon-layer `OscServer`, and that dependency lives in `GradientEngine.cpp` (compiled into the daemon binary, not the static library).

---

## Threading model

| Thread | Owner | Responsibility |
|---|---|---|
| Main thread | `GradientEngineApplication` | Lifecycle: `initialize` → `run` (blocks on `pause`) → `shutdown` on signal. |
| RtMidi MIDI callback | `mtcreceiver` (transitively) | Decodes quarter frames; fires `MtcTickSource`'s registered callback. Lock-free, non-blocking. |
| liblo network thread | `gme::daemon::comms::OscServer` | Receives UDP, dispatches to address handlers, parses, pushes to the SPSC queue. |
| (none — no worker pool) | — | Evaluation runs on the RtMidi callback thread directly. |

**Cross-thread handoff** is exactly the `LockFreeQueue<FadeCommand, 64>`:
- **Producer** — exactly one (the liblo thread inside `OscServer`).
- **Consumer** — exactly one site at a time (`GradientEngine::onTick` on the RtMidi thread). There is no 100 ms fallback drain in the current implementation; the queue is drained only on tick.

The queue is wait-free for the consumer (`pop` is a single load + indexed read + store) and bounded for the producer (drop-oldest on full, returns `false` to signal the drop).

---

## Lifecycle

### Startup

1. `main()` constructs `GradientEngineApplication` on the stack.
2. `app.initialize(argc, argv)` runs `ConfigurationManager::parseArgs`, sets up the optional `CuemsLogger`, installs signal handlers, constructs `GradientEngine`.
3. `app.run()` calls `GradientEngine::initialize({midiPort, oscPort, nodeName})`:
   - `MtcTickSource::start(midiPort)` — opens MIDI; returns `MtcStartError`. On non-`kOk`, the engine logs and the daemon exits non-zero.
   - `OscServer::start()` — binds `127.0.0.1:<port>` and starts the liblo thread. On bind failure, returns `false`; the engine logs and exits.
   - `MotionRegistry` is constructed with the tick source reference and the status callback.
   - `MtcTickSource::setTickCallback([this](long ms){ onTick(ms); })` — registration is the last step so no tick fires against a half-built engine.
4. `app.run()` blocks on `pause()`.

### Tick

For each MTC quarter frame the RtMidi thread invokes `MtcTickSource::setTickCallback`'s registered closure → `GradientEngine::onTick(mtc_ms)`:

1. Drain `queue_`: for each `FadeCommand` popped, `registry_->apply(cmd)`.
2. `registry_->tick(mtc_ms)`:
   - For each `IMotion` in `motions_`, call `evalAndSend(mtc_ms)`.
   - On `result.failed`: increment `consecutive_osc_failures`; at `kOscFailureThreshold = 5` mark for removal and emit `MotionError:"osc_send_failed"`. Otherwise reset to 0.
   - On `result.completed`: mark `completed = true`, emit `MotionComplete`, mark for removal.
   - After iterating, remove all marked motions from both `motions_` and `osc_index_`.

### Shutdown

SIGTERM / SIGINT → handler sets the loop flag → `app.run()` returns from `pause()` → `app.shutdown()`:

1. `GradientEngine::shutdown()`:
   - Deregister the tick callback (`setTickCallback({})`). The destructor of `MtcTickSource` (when the engine destructs later) blocks until any in-flight callback returns.
   - `MotionRegistry::cancelAll()` — removes every motion without `sendSnapToEnd` (final OSC values are NOT sent on shutdown).
   - `OscServer::stop()` — joins the liblo thread.
2. `ConfigurationManager` and `CuemsLogger` destruct.
3. Process exits with the code returned by `run()`.

---

## Build targets

| Target | Type | Sources | Links |
|---|---|---|---|
| `gradient_motion` | static lib | `src/{time,gradient,motion,signal,osc}/*.cpp` | `nlohmann_json::nlohmann_json`, `liblo` |
| `gradient-motiond` | executable | `daemon/{main,GradientEngineApplication}.cpp`, `daemon/config/ConfigurationManager.cpp`, `daemon/comms/OscServer.cpp`, `src/engine/GradientEngine.cpp` | `gradient_motion`, `mtcreceiver`, `rtmidi`, `liblo`, `pthread`, optional `cuemslogger` |
| `mtcreceiver` | static lib (submodule) | `mtcreceiver/*.cpp` | `rtmidi`, optional `cuemslogger` |
| `cuemslogger` | static lib (submodule) | `cuemslogger/*.cpp` | system libs (`syslog`) |
| `test_*` | executables | `tests/test_*.cpp` | `gradient_motion` + per-test extras |

CMake options:

- `BUILD_DAEMON` (default `ON`) — set to `OFF` to build only the library and tests that don't need RtMidi.
- `ENABLE_CUEMS_LOGGER` (default `ON`) — set to `OFF` to use the stub logger.
- `MTCRECV_TESTING` (forced `ON` for the daemon build) — exposes `mtcreceiver`'s test-only helpers (`invokeTickForTesting`, `SkipPortOpenTag`).

---

## Test layout

| Test | Scope | Notes |
|---|---|---|
| [`test_curves`](https://github.com/stagesoft/gradient-motion-engine/blob/main/tests/test_curves.cpp) | `gme::gradient` | Curve boundary postconditions, parameter defaults, LUT continuity. |
| [`test_mtc_tick_source`](https://github.com/stagesoft/gradient-motion-engine/blob/main/tests/test_mtc_tick_source.cpp) | `gme::time` | Uses `invokeTickForTesting`; no MIDI hardware required. |
| [`test_lockfree_queue`](https://github.com/stagesoft/gradient-motion-engine/blob/main/tests/test_lockfree_queue.cpp) | `gme::signal::LockFreeQueue` | SPSC contract, drop-oldest, advisory size. |
| [`test_fade_motion`](https://github.com/stagesoft/gradient-motion-engine/blob/main/tests/test_fade_motion.cpp) | `gme::motion::FadeMotion` | Evaluation, snap-to-end, inheritFrom. |
| [`test_motion_registry`](https://github.com/stagesoft/gradient-motion-engine/blob/main/tests/test_motion_registry.cpp) | `gme::motion::MotionRegistry` | Duplicate-id rejection, supersede, OSC-failure threshold, cancel paths. |
| [`test_motion_registry_bench`](https://github.com/stagesoft/gradient-motion-engine/blob/main/tests/test_motion_registry_bench.cpp) | perf | Per-tick cost at saturation (≤50 active motions). |
| [`test_osc_parse`](https://github.com/stagesoft/gradient-motion-engine/blob/main/tests/test_osc_parse.cpp) | `gme::signal::parseFadeOscCommand` | 14 cases; pure parse-level, no network. |
| [`test_osc_server_integration`](https://github.com/stagesoft/gradient-motion-engine/blob/main/tests/test_osc_server_integration.cpp) | `gme::daemon::comms::OscServer` | 3 real-loopback cases using liblo's client side. |
| [`bench_osc_latency`](https://github.com/stagesoft/gradient-motion-engine/blob/main/tests/bench_osc_latency.cpp) | perf | `sendFloat` syscall latency on loopback. |

End-to-end deploy tests on the running daemon live in [`dev/deploy_tests/`](https://github.com/stagesoft/gradient-motion-engine/tree/main/dev/deploy_tests): `s007_t034_smoke.sh`, `s007_t052_rate_limit.sh`, `s007_t063_multi_node.sh`, `s007_t065_avahi_resilience.sh`.

---

## Cross-repo coupling

| Repo | Coupling point | Direction |
|---|---|---|
| [`cuems-engine`](https://github.com/stagesoft/cuems-engine) (NodeEngine) | `GradientClient` sends `/gradient/start_fade` etc. over UDP to `127.0.0.1:<gradient_osc_port>`. | engine → gradient-motiond |
| [`cuems-utils`](https://github.com/stagesoft/cuems-utils) | `settings.xsd` defines `<gradient_osc_port>` on `NodeType`; `ConfigurationManager` reads it. | utils → gradient-motiond (config schema) |
| [`cuems-audioplayer`](https://github.com/stagesoft/cuems-audioplayer) | Receives `/volmaster` OSC float on its configured port. | gradient-motiond → audioplayer |
| [`cuems-videocomposer`](https://github.com/stagesoft/cuems-videocomposer) | Receives `/videocomposer/layer/{id}/opacity` OSC float. | gradient-motiond → videocomposer |
| [`mtcreceiver`](https://github.com/stagesoft/mtcreceiver) | Git submodule, pinned at `59fc76e`. v2.0.0 API contract. | dependency |
| [`cuemslogger`](https://github.com/stagesoft/cuemslogger) | Git submodule. Optional (`ENABLE_CUEMS_LOGGER=ON`). | dependency |
| [`cuems-common`](https://github.com/stagesoft/cuems-common) | Ships `cuems-gradient-motiond.service` systemd unit. | runtime dep (Debian package). |
