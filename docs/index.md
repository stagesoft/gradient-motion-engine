<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# gradient-motion-engine

**Timecode-driven motion and gradient evaluation engine for the CueMS system, with localhost UDP OSC input and OSC output.**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Tests](https://github.com/stagesoft/gradient-motion-engine/actions/workflows/tests.yml/badge.svg)](https://github.com/stagesoft/gradient-motion-engine/actions/workflows/tests.yml)
[![Coverage](https://codecov.io/gh/stagesoft/gradient-motion-engine/graph/badge.svg)](https://codecov.io/gh/stagesoft/gradient-motion-engine)
[![Deploy API documentation](https://github.com/stagesoft/gradient-motion-engine/actions/workflows/docs.yml/badge.svg)](https://github.com/stagesoft/gradient-motion-engine/actions/workflows/docs.yml)

!!! note "Project README"
    For installation instructions, release history, and licensing, see the
    [project README](https://github.com/stagesoft/gradient-motion-engine#readme) on GitHub.

!!! note "API reference"
    The full API reference is published at
    [stagesoft.github.io/gradient-motion-engine](https://stagesoft.github.io/gradient-motion-engine/)
    and built from `main` by [.github/workflows/docs.yml](../.github/workflows/docs.yml).

---

## What is gradient-motion-engine?

`gradient-motion-engine` is the per-node fade engine of the **CueMS** (Cue Management System). It runs as the `gradient-motiond` systemd service on every player node and drives parametric volume / opacity envelopes for the local audio and video players, locked to **MIDI Time Code**.

Inbound, it accepts three OSC commands over localhost UDP from the local NodeEngine — `/gradient/start_fade`, `/gradient/cancel_motion`, `/gradient/cancel_all`. Outbound, it emits OSC float updates to `cuems-audioplayer` (`/volmaster`) and `cuems-videocomposer` (`/videocomposer/layer/{id}/opacity`) on every MTC quarter frame for as long as a motion is active.

The repository ships two artifacts:

| Component | Role |
|---|---|
| `libgradient_motion` (static library) | Curves, motion registry, OSC sender, MTC tick source, lock-free queue. Transport-agnostic core; no daemon headers. |
| `gradient-motiond` (executable) | Lifecycle orchestrator, CLI/env config, liblo UDP OSC listener, wires the library into the running daemon. |

The library is link-only — there is no published C ABI. `GradientEngine.cpp` (which owns `OscServer`) is compiled into the daemon binary, not the library, so that consumers of the library are not forced to link liblo.

---

## Signal flow

```
NodeEngine (cuems-engine)
    │
    │  /gradient/start_fade  (OSC, sssisffhiss)
    │  /gradient/cancel_motion (OSC, ss)
    │  /gradient/cancel_all  (OSC, s)
    ▼
127.0.0.1:<gradient_osc_port>   ◄─── liblo UDP listener
    │                                (gme::daemon::comms::OscServer, PIMPL)
    │  parseFadeOscCommand
    │   • type-tag match
    │   • node_name filter (silent drop on mismatch)
    │   • field validation
    │   • curve_params JSON parse
    ▼
LockFreeQueue<FadeCommand, 64>      ◄─── SPSC, drop-oldest on full
    │
    │  consumer: MTC tick thread
    ▼
MtcTickSource (mtcreceiver v2)
    │  void onTick(long mtc_ms)         ◄─── 100 Hz @ 25 fps
    ▼
MotionRegistry
    │  1. drain queue, apply(cmd)
    │     • START_FADE   → MotionFactory::fromCommand → addMotion
    │     • CANCEL_MOTION → cancelMotion(id, snap_to_end)
    │     • CANCEL_ALL    → cancelAll()
    │  2. for each active motion: evalAndSend(mtc_ms)
    │  3. remove completed / dead (5 consec OSC failures) motions
    ▼
FadeMotion::evalAndSend
    │  t = clamp((mtc_ms − start_mtc_ms) / duration_ms, 0, 1)
    │  value = start + (end − start) · curve.evaluate(t)
    │  oscSend(target, path, value)
    ▼
OSC float (UDP)
    │
    ├──► cuems-audioplayer       /volmaster
    └──► cuems-videocomposer     /videocomposer/layer/{id}/opacity
```

Numbered sequence for a single fade:

1. NodeEngine sends `/gradient/start_fade` to `127.0.0.1:7100` with `node_name = "this-node"` and an OSC type tag of `sssisffhiss` (motion_id, node_name, osc_host, osc_port, osc_path, start_value, end_value, duration_ms, start_mtc_ms, curve_type, curve_params_json).
2. `OscServer` calls `parseFadeOscCommand` which type-checks, filters by node, validates required fields, and parses `curve_params_json`. On `ParseResult::Ok` the populated `FadeCommand` is pushed to the SPSC queue.
3. On the next MTC quarter-frame tick, `GradientEngine::onTick` drains the queue, calls `MotionRegistry::apply` per command.
4. For `START_FADE`, `MotionFactory::fromCommand` builds the curve via `CurveFactory::createCurve` (wrapped in a 256-sample LUT) and the `lo_address` via `gme::osc::makeAddress`, then constructs a `FadeMotion`. The registry inserts it; if the `"host:port:path"` key is already active, the prior motion is superseded and the new motion inherits its last-sent value.
5. Every subsequent tick, the registry calls `FadeMotion::evalAndSend(mtc_ms)`. When `t ≥ 1.0`, `EvalResult::completed` is `true`, the registry emits a `MotionComplete` status, and removes the motion.

---

## Architecture

### `gme::time` — MTC tick source

[`src/time/`](https://github.com/stagesoft/gradient-motion-engine/tree/main/src/time)

- **`MtcTickSource`** — Adapter over `mtcreceiver` v2.0.0. Translates v2's `void(long, bool)` to a plain `void(long mtc_ms)` so the engine doesn't see the `isCompleteFrame` flag. Lifecycle: `setTickCallback` → `start("MTC")` → `~MtcTickSource()` (blocks until any in-flight callback returns). One instance per process — `mtcreceiver` keeps process-global state.
- **`MtcStartError`** — Enum return type from `start()`: `kOk`, `kNoPortsAvailable`, `kPortNotFound`. No exceptions cross the library boundary.

### `gme::gradient` — Curve evaluation

[`src/gradient/`](https://github.com/stagesoft/gradient-motion-engine/tree/main/src/gradient)

- **`Curve`** — Abstract interface; `evaluate(double t) const → double`. Concrete implementations clamp `t` to `[0,1]` internally. Boundary postcondition: `evaluate(0.0) == 0.0` and `evaluate(1.0) == 1.0` for all bundled curves.
- **`LinearCurve`**, **`SigmoidCurve`**, **`BezierCurve`**, **`EaseInCurve`**, **`EaseOutCurve`**, **`SCurve`** — Concrete shapes.
- **`ScaledCurve`** — Decorator that scales another curve's output range.
- **`ResampledCurve`** — Decorator that pre-samples any curve into a 256-entry LUT. Every curve returned by `CurveFactory` is wrapped in this so the hot path is constant-time.
- **`CrossfadePair`** — Two-curve container reserved for the deferred crossfade motion type.
- **`CurveFactory`** — Single construction entry point: `createCurve(type, params) → std::optional<unique_ptr<Curve>>`. Unknown types return `nullopt` so the caller decides whether to fall back to linear or reject the command.

### `gme::motion` — Motion lifecycle and registry

[`src/motion/`](https://github.com/stagesoft/gradient-motion-engine/tree/main/src/motion)

- **`IMotion`** — Abstract base. Common public fields: `motion_id`, `osc_key`, `start_mtc_ms`, `duration_ms`, `completed`, `consecutive_osc_failures`. Three virtuals: `evalAndSend`, `sendSnapToEnd`, `inheritFrom`. Type-specific state (curve, transport handle) lives in derived classes only.
- **`EvalResult`** — POD `{completed, failed, failure_reason}` returned from `evalAndSend`. `failure_reason` is a static-storage `const char*` to keep the hot path heap-free.
- **`FadeMotion`** — Concrete scalar-fade. Owns a pre-resampled `Curve`, scalar `start_value` / `end_value` / `last_sent_value`, and a pre-built `lo_address`. `inheritFrom` copies the prior fade's `last_sent_value` into `start_value` and `last_sent_value` to avoid a jump on supersede. Type-mismatched supersede (e.g. a future `VectorMotion<3>` superseding a `FadeMotion`) is a no-op.
- **`MotionFactory`** — Stateless construction site. The single place in the codebase that calls `CurveFactory::createCurve` and `lo_address_new`. On construction failure (`unknown_curve_type`, `osc_address_failed`) it emits a status event and returns `nullptr`; the registry never calls `addMotion(nullptr)`.
- **`MotionRegistry`** — Owns every live motion. Two indexes: `motions_` (`motion_id → IMotion`) and `osc_index_` (`"host:port:path" → motion_id`). `addMotion` runs ordered checks: duplicate-`motion_id` guard, then `osc_key` supersede, then insert. `tick(mtc_ms)` evaluates every motion, increments `consecutive_osc_failures` on failed sends, declares dead at `kOscFailureThreshold = 5` consecutive failures (25 ms of silence at 200 Hz), and removes completed/dead motions in a single pass.

### `gme::signal` — Commands, parser, status queue

[`src/signal/`](https://github.com/stagesoft/gradient-motion-engine/tree/main/src/signal)

- **`FadeCommand`** — Plain aggregate. Carries every field needed for `START_FADE`, `CANCEL_MOTION`, `CANCEL_ALL`, and (deferred) `START_CROSSFADE`. The sole payload type moved between the OSC thread and the tick thread.
- **`ParseResult`** — Outcome enum. `Ok` is the only success; `NodeMismatch` is a silent drop; `MissingField`/`TypeError` log a warning; `UnknownCommand` logs at warning level.
- **`StatusKind`** — Discriminates `MotionComplete` from `MotionError` for journal logging.
- **`parseFadeOscCommand`** — Free function (`gme::signal`). Dispatches on the OSC address, validates the type tag against the address's required signature, applies the `node_name` filter, validates required fields, and parses `curve_params_json` via `nlohmann::json`. Partial-population rule: `motion_id` and `type` are set before any early return so callers can log the offending command.
- **`LockFreeQueue<T, N>`** — Fixed-capacity SPSC ring buffer. Zero heap allocation after construction. Drop-oldest on full (returns `false` so the caller can log the overflow). Usable capacity is `N - 1` (one slot reserved to distinguish empty from full without a third atomic).

### `gme::osc` — Transport sender

[`src/osc/`](https://github.com/stagesoft/gradient-motion-engine/tree/main/src/osc)

- **`sendFloat(lo_address, const char* path, float)`** — Stateless liblo wrapper, `noexcept`. UDP fire-and-forget on loopback; ~1–5 µs per call. Safe from the MTC tick thread under the documented loopback assumption.
- **`makeAddress(host, port)`** — C++-friendly wrapper over `lo_address_new`. Caller owns the returned handle and must call `lo_address_free` on destruction (in practice, `FadeMotion`'s destructor).

### `gme::engine` — Orchestrator

[`src/engine/`](https://github.com/stagesoft/gradient-motion-engine/tree/main/src/engine)

- **`GradientEngine`** — Owns `MtcTickSource`, `LockFreeQueue<FadeCommand,64>`, `OscServer`, and `MotionRegistry`. `initialize({midiPort, oscPort, nodeName})` opens MIDI, binds the OSC socket, and starts the listener; `shutdown()` deregisters the tick callback, cancels every motion (no final OSC), and joins the liblo thread. `OscServer` is forward-declared in the header so consumers of `libgradient_motion` don't transitively pull in liblo or daemon headers.

### Daemon layer

[`daemon/`](https://github.com/stagesoft/gradient-motion-engine/tree/main/daemon)

- **`GradientEngineApplication`** — Lifecycle states `Constructed → Initialized → Running → Shutting Down → Destroyed`. Installs SIGTERM/SIGINT handlers; owns `ConfigurationManager` and the optional `CuemsLogger`.
- **`ConfigurationManager`** — `getopt_long` parser. Flags: `--midi-port`, `--log-level`, `--conf-path`, `--osc-port`, `--node-name`, `--help`, `--version`. OSC port priority: CLI flag → `CUEMS_GRADIENT_OSC_PORT` env → `settings.xml`'s `<gradient_osc_port>` → compile-time default `7100`.
- **`gme::daemon::comms::OscServer`** — liblo UDP listener. Binds `127.0.0.1:<port>` only — never a routable interface. PIMPL pattern keeps liblo headers out of library consumers. Three method handlers: `/gradient/start_fade` (type tag `sssisffhiss`), `/gradient/cancel_motion` (`ss`), `/gradient/cancel_all` (`s`).

---

## Key design decisions

### Real-time safety: zero allocation on the tick path

The MTC quarter-frame callback is the system's hardest deadline (100 Hz at 25 fps, 200 Hz at 50 fps). On the tick path, `MotionRegistry::tick` calls `IMotion::evalAndSend` for every active motion, which evaluates the curve LUT, computes the lerp, and calls `lo_send`. None of these steps allocate. The LUT is built once at `START_FADE` time; the `lo_address` is built once at the same moment. The SPSC queue is fixed-size in-class storage.

**Invariant:** no path from `onTick` through `evalAndSend` to `lo_send` allocates or blocks.

**Why not a thread-pool/futures-based design?** A scheduled pool would shift the deadline from "before next quarter-frame" to "before pool dispatch latency" — opaque, jittery, and untestable. The tick callback is already the right place to do the work because `mtcreceiver` provides the precise scheduling we need.

### Localhost-only inbound transport

`OscServer` binds `127.0.0.1` explicitly. Even if the host has a routable interface, the listener will not accept commands from it.

**Invariant:** `gradient-motiond` accepts commands only from processes running on the same machine.

**Why not bind 0.0.0.0?** Inbound network commands belong to the NodeEngine layer (`cuems-engine`), which authenticates and authorises against the node fleet topology. Letting `gradient-motiond` accept network OSC directly would duplicate that responsibility and create a bypass.

### Exceptions do not cross the library boundary

Every fallible operation in `libgradient_motion` returns an enum or `std::optional`: `MtcStartError`, `ParseResult`, `EvalResult::failed`, `CurveFactory::createCurve → std::optional<unique_ptr<Curve>>`. Constructors that need to fail (e.g. `lo_address_new` returning `nullptr`) are pushed into `MotionFactory::fromCommand`, which returns `nullptr` on failure.

**Invariant:** `libgradient_motion` callers never need a `try/catch` to use the library safely.

**Why not exceptions?** The daemon links against `cuemslogger` and `mtcreceiver`, both of which use `noexcept` interfaces. Mixing exception throwers into the tick path would force `noexcept(false)` propagation through every callback and defeat compiler optimisations on the hot path.

### Open/closed for motion types

New motion kinds (e.g. `VectorMotion<3>` for RGB cues, `PoseMotion` for spatial trajectories) subclass `IMotion` and register through `MotionFactory::fromCommand`. `MotionRegistry` is sealed — it does not change shape when a new motion type is added.

**Invariant:** adding a new motion type touches three files: `IMotion`'s subclass header, its `.cpp`, and the switch in `MotionFactory::fromCommand`.

**Why not a registry of registries?** A type-erased switch in one factory is simpler than a polymorphic registration mechanism. The set of motion types is small and stable; OCP without overengineering.

### Supersede inherits state via `inheritFrom`

When a new `FadeMotion` is added on a `"host:port:path"` already covered by an active fade, the old motion is removed and `new_motion->inheritFrom(old_motion)` is called. `FadeMotion::inheritFrom` dynamic-casts to `const FadeMotion*` and, on success, copies `prior.last_sent_value` into `this->start_value` and `this->last_sent_value`. Type-mismatched supersede is a no-op (the new motion starts from its declared `start_value`).

**Invariant:** the OSC output stream on a given `"host:port:path"` is continuous across supersede — no value jumps.

**Why not snap to the new motion's `start_value`?** The cue designer's intent is "go from where we are to the new target", not "jump to the new start". Letting the old `last_sent_value` set the new fade's effective `start_value` preserves that intent.

### `OscServer` lives in the daemon layer, not the library

`OscServer` depends on liblo and on the `parseFadeOscCommand` parser. `GradientEngine.h` forward-declares `OscServer` and `GradientEngine.cpp` is compiled into the daemon binary, not the library. This means a library consumer that wants to drive the engine with a non-OSC transport can do so without linking liblo.

**Invariant:** `libgradient_motion`'s link closure does not contain liblo unless an OSC-using consumer pulls it in transitively.

**Why not put `OscServer` in the library?** Coupling the library's link closure to liblo would force every embedder to depend on it, even when they don't use OSC input. The forward-declaration trick keeps the option open.

---

## API reference

The API reference is published at [stagesoft.github.io/gradient-motion-engine](https://stagesoft.github.io/gradient-motion-engine/) and built from `main`. Build it locally with:

```bash
pip install mkdocs mkdocs-material mkdoxy
mkdocs serve
# Browse to http://127.0.0.1:8000
```

The pages below are the hand-written architecture overview:

- [Architecture](architecture.md) — module-by-module component graph and dependencies.
- [API synopsis](api.md) — public header surface of `libgradient_motion` and the daemon.
