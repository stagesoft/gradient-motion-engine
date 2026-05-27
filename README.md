<!--
***
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
***
-->

# gradient-motion-engine

**Current release: v0.3.0** — see [CHANGELOG.md](./CHANGELOG.md).

**Timecode-driven motion and gradient evaluation engine, with localhost UDP OSC input and OSC output.**

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Tests](https://github.com/stagesoft/gradient-motion-engine/actions/workflows/tests.yml/badge.svg)](https://github.com/stagesoft/gradient-motion-engine/actions/workflows/tests.yml)
[![codecov](https://codecov.io/gh/stagesoft/gradient-motion-engine/graph/badge.svg?token=8QZJTRJU0I)](https://codecov.io/gh/stagesoft/gradient-motion-engine)
[![Deploy API documentation](https://github.com/stagesoft/gradient-motion-engine/actions/workflows/docs.yml/badge.svg)](https://github.com/stagesoft/gradient-motion-engine/actions/workflows/docs.yml)

* **Source / issues:** [stagesoft/gradient-motion-engine](https://github.com/stagesoft/gradient-motion-engine) on GitHub
* **API reference (HTML):** [stagesoft.github.io/gradient-motion-engine](https://stagesoft.github.io/gradient-motion-engine/) (Doxygen, built from `main` via GitHub Pages)

`gradient-motion-engine` is the per-node fade and motion engine developed for the **CueMS** (Cue Management System). It evaluates parametric volume/opacity envelopes (and, in time, generalised motion trajectories) locked to **MIDI Time Code**, receiving commands over **localhost UDP OSC** from the local NodeEngine and emitting **OSC** value updates to audio (`cuems-audioplayer /volmaster`) and video (`cuems-videocomposer /videocomposer/layer/{id}/opacity`) players.

It is composed of:

* **`libgradient_motion`** — a reusable C++17 static library providing the evaluation core: curves, motion registry, OSC sender, MTC tick source, and the lock-free command queue.
* **`gradient-motiond`** — a systemd-managed daemon that wires the library to a liblo UDP OSC listener and runs the evaluation pipeline.

---

## Overview

The engine models time-dependent behaviour as a deterministic pipeline:

```text
NodeEngine ──► OscServer ──► LockFreeQueue<FadeCommand,64> ──► MotionRegistry
   (UDP)       (liblo)         (SPSC, drop-oldest)             │
                                                               │  per MTC quarter-frame tick
                                                               ▼
                                                          IMotion::evalAndSend
                                                               │
                                                               ▼
                                                          OSC float ──► AudioPlayer / VideoComposer
```

* **OSC input** — `/gradient/start_fade`, `/gradient/cancel_motion`, `/gradient/cancel_all` arrive on `127.0.0.1:<gradient_osc_port>` (default `7100`).
* **Parse + filter** — `parseFadeOscCommand` validates the type-tag, applies the `node_name` filter, and produces a `FadeCommand`.
* **Queue** — `LockFreeQueue<FadeCommand, 64>` hands the command from the liblo network thread to the MTC tick thread (drop-oldest on overflow).
* **Tick loop** — `MtcTickSource` fires on every MTC quarter frame (100 Hz at 25 fps); `MotionRegistry` drains the queue, applies commands, then calls `IMotion::evalAndSend` on every active motion.
* **OSC output** — `FadeMotion` interpolates `start_value → end_value` along a pre-resampled `Curve` (256-sample LUT) and emits a single OSC float per active motion per tick to the target player.

---

## Architecture

### Library: `libgradient_motion` (`src/`)

#### `gme::time` — [src/time/](src/time/)

* **`MtcTickSource`** — Thin adapter over `mtcreceiver` v2.0.0 exposing a `void(long mtc_ms)` callback. One-instance-per-process (mtcreceiver uses static state); blocks any in-flight callback in its destructor. Lock-free, non-blocking callback path on the RtMidi thread.
* **`MtcStartError`** — Enum: `kOk`, `kNoPortsAvailable`, `kPortNotFound` (no exceptions across the library boundary).

#### `gme::gradient` — [src/gradient/](src/gradient/)

* **`Curve`** — Abstract interface; maps normalised `t ∈ [0,1]` to normalised output `[0,1]`. Concrete types clamp internally; `evaluate(0.0) == 0.0` and `evaluate(1.0) == 1.0` for all bundled types.
* **`LinearCurve`** — Identity mapping.
* **`SigmoidCurve`** — Logistic sigmoid with configurable `steepness` (default 8.0) and `midpoint` (default 0.5).
* **`BezierCurve`** — Cubic Bézier; control points `(cx1, cy1, cx2, cy2)` with documented defaults.
* **`EaseInCurve` / `EaseOutCurve`** — Power-ease shapes (`exponent`, default 2.0).
* **`SCurve`** — Smoothstep variant for symmetric ease-in/ease-out.
* **`ScaledCurve`** — Decorator that scales another curve's output range.
* **`ResampledCurve`** — Decorator that pre-samples any curve into a 256-entry LUT; this is the wrapper applied to every curve returned by `CurveFactory` so that runtime evaluation is constant-time.
* **`CrossfadePair`** — Two-curve container reserved for the deferred crossfade motion type.
* **`CurveFactory`** — Single entry point: `createCurve(type, params) → std::optional<unique_ptr<Curve>>`. Unknown types return `nullopt` so the caller decides the fallback policy.

#### `gme::motion` — [src/motion/](src/motion/)

* **`IMotion`** — Abstract base for all motion types. Owns the common lifecycle fields (`motion_id`, `osc_key`, `start_mtc_ms`, `duration_ms`, `completed`, `consecutive_osc_failures`). Three virtuals: `evalAndSend`, `sendSnapToEnd`, `inheritFrom`. New motion kinds extend this, not `MotionRegistry`.
* **`EvalResult`** — POD result from `evalAndSend`: `completed`, `failed`, and a static-storage `failure_reason` string.
* **`FadeMotion`** — Concrete scalar-fade motion. Owns a pre-resampled `Curve`, scalar `start_value`/`end_value`/`last_sent_value`, and a pre-built `lo_address`. `inheritFrom` copies the prior motion's last-sent value to avoid jumps on supersede.
* **`MotionFactory`** — Stateless construction site. `fromCommand(cmd, ctx)` is the single place where `CurveFactory::createCurve` and `lo_address_new` are called.
* **`MotionRegistry`** — Owns every active motion, indexed by `motion_id` (primary) and `"host:port:path"` (secondary, for supersede). Single-threaded API (MTC tick thread). Per-tick: drain, apply, evaluate, remove completed/dead motions. Supersede inherits state; OSC failure threshold is `kOscFailureThreshold = 5` consecutive errors.

#### `gme::signal` — [src/signal/](src/signal/)

* **`FadeCommand`** — Plain aggregate carrying every field needed for the four command types (`START_FADE`, `CANCEL_MOTION`, `CANCEL_ALL`, `START_CROSSFADE`). The sole payload moved between the OSC thread and the tick thread.
* **`ParseResult`** — Outcome enum returned by `parseFadeOscCommand` (`Ok`, `NodeMismatch`, `MissingField`, `TypeError`, `UnknownCommand`, …).
* **`StatusKind`** — Discriminates `MotionComplete` from `MotionError` for journal logging.
* **`parseFadeOscCommand`** — Free function that validates the type-tag, applies the `node_name` filter, validates field constraints, parses `curve_params_json`, and partially populates `motion_id` / `type` on `MissingField` / `TypeError` so callers can log the rejection.
* **`LockFreeQueue<T, N>`** — Fixed-capacity SPSC ring buffer with drop-oldest-on-full. Zero heap allocation after construction; bounded producer path; advisory `size()`/`empty()`.

#### `gme::osc` — [src/osc/](src/osc/)

* **`sendFloat(target, path, value)`** — Stateless liblo wrapper. Safe to call from the MTC tick thread under the loopback assumption (UDP fire-and-forget; ~1–5 µs per call).
* **`makeAddress(host, port)`** — Thin C++ wrapper over `lo_address_new`; caller owns the returned handle.

#### `gme::engine` — [src/engine/](src/engine/)

* **`GradientEngine`** — Top-level orchestrator. Owns `MtcTickSource`, `LockFreeQueue<FadeCommand,64>`, `OscServer` (via forward declaration to keep liblo headers out of library consumers), and `MotionRegistry`. `GradientEngine.cpp` compiles into the daemon binary, not the library, since `OscServer` lives in the daemon layer.

### Daemon: `gradient-motiond` (`daemon/`)

* **`GradientEngineApplication`** — Lifecycle orchestrator (`Constructed → Initialized → Running → Shutting Down → Destroyed`). Installs SIGTERM/SIGINT handlers; owns `ConfigurationManager` and the optional `CuemsLogger`.
* **`ConfigurationManager`** — Parses CLI flags via `getopt_long`. Resolves `gradient_osc_port` in priority order: `--osc-port` → `CUEMS_GRADIENT_OSC_PORT` → `settings.xml` `<gradient_osc_port>` → compile-time default `7100`.
* **`gme::daemon::comms::OscServer`** — liblo UDP listener bound to `127.0.0.1:<port>` (never a routable interface). Registers handlers for `/gradient/start_fade` (`sssisffhiss`), `/gradient/cancel_motion` (`ss`), `/gradient/cancel_all` (`s`). PIMPL pattern keeps liblo headers out of library consumers.

---

## Core Concepts

* **Motion** — A time-bounded transformation of an OSC parameter (currently a scalar fade). Indexed in the registry by a caller-assigned `motion_id`.
* **OSC supersede key** — Composite `"host:port:path"`. At most one active motion per key; a new motion on the same key supersedes the old and inherits its last-sent value.
* **MTC tick** — A quarter-frame callback from `mtcreceiver`; the only thread on which evaluation runs.
* **Curve** — A normalised `[0,1] → [0,1]` shaping function. Every curve handed to a `FadeMotion` is wrapped in a 256-sample LUT for constant-time evaluation.
* **FadeCommand** — The wire-and-queue payload type. Built by the OSC parser, drained by the registry, never persisted.
* **Node name filter** — Every inbound OSC command carries the target `node_name`; the listener silently drops commands targeted at other nodes.

---

## Design Goals

* **Deterministic** — Identical MTC inputs and commands produce identical OSC outputs.
* **Real-time capable** — The evaluation tick path is lock-free and zero-allocation; transport handles are pre-built at `START_FADE` time.
* **Exception-free across the library boundary** — Errors propagate as enum return values (`MtcStartError`, `ParseResult`, `EvalResult::failed`), never as thrown exceptions.
* **Open for extension, closed for modification** — New motion kinds subclass `IMotion` and register through `MotionFactory`; `MotionRegistry` does not need to change.
* **Embeddable** — `libgradient_motion` has no daemon dependencies. `GradientEngine.cpp` compiles into the daemon binary so the library remains transport-agnostic at link time.
* **Localhost-only inbound transport** — The OSC listener binds `127.0.0.1` exclusively; commands from the network must traverse into localhost.

---

## Installation

### Build from source

System packages (Debian/Ubuntu):

```bash
sudo apt-get install -y \
  build-essential cmake pkg-config \
  librtmidi-dev liblo-dev nlohmann-json3-dev libtinyxml2-dev
```

Configure and build:

```bash
git clone --recursive https://github.com/stagesoft/gradient-motion-engine.git
cd gradient-motion-engine
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
```

If you cloned without `--recursive`, fetch the submodules:

```bash
git submodule update --init --recursive
```

### Debian package

The `debian/` directory carries packaging metadata for building a native `.deb`:

```bash
git clone https://github.com/stagesoft/gradient-motion-engine.git
cd gradient-motion-engine
dpkg-buildpackage -us -uc
sudo dpkg -i ../cuems-gradient-motiond_*.deb
```

The `cuems-gradient-motiond` package installs the binary at `/usr/bin/gradient-motiond`. The matching `cuems-gradient-motiond.service` systemd unit is shipped by the `cuems-common` package (declared as a runtime dependency).

### systemd service

```bash
systemctl enable cuems-gradient-motiond
systemctl start cuems-gradient-motiond
```

---

## Development

### Build with tests and debug symbols

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)
ctest --output-on-failure
```

### Build with coverage

```bash
mkdir build && cd build
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="--coverage" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage"
make -j$(nproc)
ctest --output-on-failure
lcov --capture --directory . --output-file coverage.info --ignore-errors inconsistent
lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
```

### Library-only build (no MIDI/daemon)

For embedding the library in another project without RtMidi available:

```bash
cmake -B build -DBUILD_DAEMON=OFF
cmake --build build
```

### Generate API docs (Doxygen)

```bash
cmake -B build
cmake --build build --target docs
# Output: build/docs/html/index.html
```

### Run a specific test

```bash
ctest --test-dir build -R test_osc_parse --output-on-failure
ctest --test-dir build -R test_osc_server_integration --output-on-failure
ctest --test-dir build -R test_motion_registry --output-on-failure
```

### Deploy / smoke tests

End-to-end scripts that exercise the running daemon over OSC live in [dev/deploy_tests/](dev/deploy_tests/) — `s007_t034_smoke.sh`, `s007_t052_rate_limit.sh`, `s007_t063_multi_node.sh`, `s007_t065_avahi_resilience.sh`.

---

## Release notes

See [CHANGELOG.md](./CHANGELOG.md) for the full history.

### v0.3.0 — 2026-05-13 — OSC Input Transport

Replaces the NNG bus-client inbound transport with a localhost UDP OSC listener. New `OscServer` (liblo, PIMPL) binds `127.0.0.1:<gradient_osc_port>` and handles `/gradient/start_fade`, `/gradient/cancel_motion`, `/gradient/cancel_all`. Adds `parseFadeOscCommand` (pure C++ free function, `nlohmann::json` for `curve_params_json`), `--osc-port` CLI flag (default `7100`), and `CUEMS_GRADIENT_OSC_PORT` env override. Removes the NNG bus client, the status-emit queue, and the JSON `parseFadeCommand`. Renames `fade_id` → `motion_id` and `partner_fade_id` → `partner_motion_id` for ecosystem consistency. 14 parse-level unit tests + 3 real-loopback integration tests. Full spec: [specs/007-osc-input-transport/](specs/007-osc-input-transport/).

### v0.1.0 — 2026-04-23 — First public release

Initial timecode-driven motion and gradient evaluation with OSC output. Ships `libgradient_motion` (modular namespaces `gme::time`, `gme::gradient`, `gme::motion`, `gme::signal`, `gme::osc`, `gme::engine`) with pluggable curve types (linear, sigmoid, bezier, ease-in/out, scurve, resampled, scaled, crossfade pair) and factory-based construction. Includes MTC tick source built on `mtcreceiver` v2.0.0, polymorphic `IMotion` hierarchy with duplicate-id rejection and supersede inheritance, fade-registry tick loop, lock-free SPSC queue, and the `gradient-motiond` systemd daemon. Tests cover curves, MTC, NNG (pre-0.3.0), lock-free queue, fade/motion registry, and OSC latency benchmarks.

---

## Copyright notice

Copyright © 2026 Stagelab Coop SCCL. Authors include Adrià Masip (`adria@stagelab.coop`).

This work is part of **gradient-motion-engine**. It is free software: you can redistribute it and/or modify it under the terms of the **GNU General Public License** as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but **without any warranty**; without even the implied warranty of **merchantability** or **fitness for a particular purpose**. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see [https://www.gnu.org/licenses/](https://www.gnu.org/licenses/).

The SPDX short form of this notice is: `SPDX-License-Identifier: GPL-3.0-or-later`.

---

## License

This project is licensed under the terms of the **GNU General Public License v3.0 or later (GPL-3.0-or-later)**.

You are free to use, modify, and redistribute this software under the conditions set by the license. Any derivative work must also be distributed under the same license terms.

See the [LICENSE](./LICENSE) file for the full license text.

---

### Summary of Terms

* **Permissions**:

  * Use for any purpose
  * Study and modify the source code
  * Redistribute original or modified versions

* **Conditions**:

  * Source code must be made available when distributing
  * Modifications must be licensed under GPL v3 or later
  * Include a copy of the license and preserve notices

* **Limitations**:

  * Provided *without warranty*
  * No liability for damages or misuse

---
