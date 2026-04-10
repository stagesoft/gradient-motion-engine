
# Gradient Motion Engine

**Timecode-driven motion and gradient evaluation engine with OSC output.**

`gradient-motion-engine` is a C++ system for defining and evaluating **time-based motion trajectories** and **value gradients**, producing structured **OSC (Open Sound Control)** messages in real time.

It is composed of:

* **`libgradient_motion`** — a reusable C++ library providing core primitives (time, motion, gradients, signal evaluation)
* **`gradient-motiond`** — a systemd-managed daemon that runs the evaluation engine and emits OSC

---

## Overview

The engine models time-dependent behavior as a deterministic pipeline:

```text
Timecode → Motion → Gradient → Signal → OSC
```

* **Timecode** drives the system clock and scheduling
* **Motion** resolves spatial trajectories over time
* **Gradient** maps time or position to interpolated values
* **Signal** structures evaluated data into frames
* **OSC** transports the resulting data to external systems

This separation allows `libgradient_motion` to be embedded in other applications while `gradient-motiond` provides a ready-to-run runtime for Linux environments.

---

## Architecture

### Library: `libgradient_motion`

The core library exposes modular components organized by domain:

* `gme::time` — timecode, clocks, scheduling
* `gme::motion` — trajectories and spatial evaluation
* `gme::gradient` — keyframes and interpolation
* `gme::signal` — evaluated value representation
* `gme::osc` — OSC encoding and transport
* `gme::engine` — orchestration and execution pipeline

The library is designed for:

* deterministic evaluation
* low-latency execution
* composability and embedding

---

### Daemon: `gradient-motiond`

`gradient-motiond` is the runtime service built on top of `libgradient_motion`.

Responsibilities:

* load configuration (`/etc/gradient-motion/`)
* manage timecode source
* execute the evaluation pipeline
* emit OSC messages to configured endpoints

Typical deployment:

```bash
systemctl enable gradient-motiond
systemctl start gradient-motiond
```

---

## Core Concepts

* **Timecode** — the authoritative temporal reference for all evaluation
* **Trajectory** — defines motion through space over time
* **Gradient** — defines value interpolation across time or position
* **Signal Frame** — evaluated state at a given time step
* **OSC Output** — serialized messages emitted to external systems

---

## Design Goals

* **Deterministic** — identical inputs produce identical outputs
* **Modular** — clean separation between motion, gradients, and transport
* **Real-time capable** — suitable for continuous execution under systemd
* **Embeddable** — core logic available via `libgradient_motion`
* **Protocol-agnostic core** — OSC isolated to the output layer

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
