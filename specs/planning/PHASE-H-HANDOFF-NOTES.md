<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileContributor: Ion Reguera <ion@stagelab.coop>
-->

# Phase H — handoff notes for Adrià

Hi Adrià,

I'm working on integrating `gradient-motion-engine` with `cuems-engine` (ClickUp [869d6vuux](https://app.clickup.com/t/869d6vuux), PR #12). I want to share what I found and propose a refactor before making changes that touch your work.

This is a summary. Full details in [`phase-h-osc-refactor-plan.md`](phase-h-osc-refactor-plan.md) in this same `docs/` folder.

## TL;DR

NNG bus0 in star topology doesn't auto-relay between dialers. The original spec assumed it did. Result: the daemon never receives `start_fade` from NodeEngine in production. I'm proposing to refactor the daemon's command intake from NNG to UDP OSC (matching the AudioPlayer/VideoComposer/DmxPlayer pattern). All your motion logic (FadeMotion, MotionRegistry, MotionFactory, curves, MtcTickSource) is reused as-is. Only the recv path in `daemon/comms/` changes.

## What I found during integration testing

### 1. Three wire-format mismatches in PR #12 (cuems-engine side)

Already fixed on `merge/fade-cue-staging` of cuems-engine, branched off your `feat/fade-cue`:

- Python `send_fade_command` emitted `target_value` and `start_time`; C++ `parseFadeCommand` expects `end_value` and `start_mtc_ms`. Now renamed in Python.
- `target_value` was an integer 0–100 (FadeCue UI scale); `end_value` is a float 0.0–1.0 (OSC scale). Now divided by 100 before send. Without this normalization, gradient-motion-engine forwards `80.0` directly to `/volmaster` which expects 0.0–1.0, blowing the volume to clipping.
- Python omitted `node_name` from both `start_fade` and `cancel_all` payloads. C++ side correctly rejected them as `NodeMismatch`/`MissingField`. Now injected in both.

Also fixed an unrelated dispatch bug in `_handle_fade_action` (it was receiving the resolved target instead of the FadeCue itself, crashing on `cue._action_target_object`). Plus `frozen_mtc_ms` threading per rc_1's pattern. Plus the `_send_gradient_cancel_all` in ControllerEngine had a wrong API call (`communications_thread.send_operation` doesn't exist; should be `nng_hub.send_operation` wrapped in `asyncio.run_coroutine_threadsafe`).

All of the above is in 7 commits on `merge/fade-cue-staging` of cuems-engine. Phase A in the plan.

### 2. The architectural break

After the wire fixes, the daemon STILL didn't receive `start_fade`. I traced through:

- Daemon's NNG dialer was connected to controller's listener (verified via `ss -tnp`)
- MTC was reaching the daemon (verified via `aseqdump` + instrumented `onTick` logs)
- recvLoop was running (instrumented with iteration counters)
- Controller's listener WAS receiving `start_fade` from NodeEngine (engine logs prove it)
- Daemon's recvLoop was NOT receiving the same message

I tested the bus0 topology with a minimal 3-peer Python script:

```
listener (Bus0)
  ↑ ↑
  A B  (two dialers)

A.send(...) → only listener receives. B does NOT receive.
B.send(...) → only listener receives. A does NOT receive.
listener.send(...) → both A and B receive.
```

So in star topology, **the listener doesn't auto-relay between dialers**. This contradicts the spec's stated assumption ("bus0 is broadcast — every node sees every message"). It's an empirical fact about pynng `Bus0` LISTENER + `Bus0` DIALERs in star topology — confirmed in the test. The spec was written without testing this.

For node-engine's `start_fade` to reach the daemon, the controller would need to actively relay. There's no relay code today.

### 3. Status broadcasts have no consumers

I checked who consumes the daemon's `motion_complete` and `motion_error` broadcasts:

- `cuems-engine/src/cuemsengine/comms/NodeCommunications.py` line 106 — `_handle_status_operation` explicitly **discards** them at debug level. Comment says "the Python engine no longer mutates state in response to them (general cue lifecycle handles all disarm)".
- `ControllerEngine.status_operation_callback` line 423 — silent guard returns immediately for `gradientengine_*` senders.
- `cuems-editor` — no references.
- `cuems-frontend` — zero references to fade_progress, fade_status, motion_*.

The cue lifecycle (start/end) is already tracked by `loop_fadeCue` in `cuems-engine/src/cuemsengine/cues/loop_cue.py` (lines 45–63). Same pattern as `loop_audioCue`/`loop_videoCue`. The daemon's status broadcasts duplicate that work.

## Proposed refactor — Phase H

### What it changes

The daemon's **input** transport: NNG bus → UDP OSC server.

```
Before:                              After:
NodeEngine ──NNG bus──→ Controller   NodeEngine ──UDP OSC──→ gradient-motiond
                ↓ (broken)             (localhost, port 7100, oscpack listener)
            gradient-motiond

(Daemon also dials controller for     (No controller involvement on the
 status — see below)                   command path)
```

The daemon's **output** transport (OSC to /volmaster, /opacity, etc.) is unchanged.

Status emission from the daemon is **removed**. The future fade-progress UI feature lands in NodeEngine's `loop_fadeCue` (where it belongs, matching `loop_audioCue`/`loop_videoCue`/`loop_dmxCue`). NodeEngine already knows `_start_mtc` and `_end_mtc` and can compute progress without involving the daemon. Look at `loop_audioCue` lines 95–104 — there's commented-out progress emission code that anticipates this exact pattern.

### What stays the same (your work, untouched)

- `IMotion` + `FadeMotion`
- `MotionRegistry` (apply/tick/addMotion/cancelMotion/cancelAll/supersede)
- `MotionFactory`
- `LockFreeQueue<T, N>` — still used for OSC-thread → tick-thread handoff
- `MtcTickSource` + bundled `mtcreceiver` submodule
- All curves (`BezierCurve`, `CurveFactory`, JSON curve_params)
- All motion tests (test_fade_motion, test_motion_registry, test_lockfree_queue, test_curve_factory)
- `FadeCommand` struct — just constructed from OSC parse instead of JSON parse
- `Type::START_CROSSFADE` enum + `partner_fade_id` field — Phase 7 prep stays
- `VectorMotion<N>` design — single-path multi-arg OSC fits perfectly

### What changes (in this repo)

- DELETE: `daemon/comms/NngBusClient.{cpp,h}` (the recv loop is the broken one)
- DELETE: `src/signal/StatusEmitRequest.h` (status emit no longer needed)
- ADD: `daemon/comms/OscServer.{cpp,h}` — wraps oscpack like `cuems-audioplayer/src/oscreceiver/oscreceiver.cpp` does
- ADD: `src/signal/parseFadeOscCommand.{cpp,h}` — mirrors the existing JSON parser, same `ParseResult` semantics, same target/node_name filter
- MODIFY: `src/engine/GradientEngine.cpp` — replace `nngClient_` member with `oscServer_`, otherwise same shape
- MODIFY: `daemon/main.cpp` — drop `--nng-url`, add `--osc-port` (default 7100)
- MODIFY: `CMakeLists.txt`, `debian/control` — drop `libnng-dev`, add `liboscpack-dev`

In cuems-engine: add a `GradientPlayer.py` following `VideoPlayer.py`/`DmxPlayer.py` pattern; rewire `_handle_fade_action` to use it; delete `_send_gradient_cancel_all`; move cancel_all to NodeEngine.

In cuems-common: drop `--nng-url` from the systemd unit, drop the avahi `Wants=`/`After=`.

### Future motion types

All planned motion types share the same output primitive (emit N OSC floats to a path at MTC tick rate). The OSC refactor only changes the input transport, not the output:

| Motion | OSC output | Affected? |
|---|---|---|
| FadeMotion | `/volmaster <float>` etc. | No |
| Opacity fade | `/videocomposer/layer/{id}/opacity <float>` | No |
| `VectorMotion<2>` | `/position <float> <float>` | No |
| Crossfade (Phase 7) | two paired FadeMotions, lockstep OSC | No |

Bonus: oscpack supports OSC bundles, useful for Phase-7 crossfade lockstep updates. NNG had no equivalent.

## Why I'm proposing this

You chose NNG citing "match the CUEMS ecosystem" (research.md Decision 1, alternatives section). On reflection, gradient-motiond is a **Plane-2 player** — runs on each node, receives commands from local NodeEngine, drives an OSC sink — not a Plane-1 inter-node bus participant. The other Plane-2 players are pure-OSC. Aligning gradient-motiond with the Plane-2 pattern fits the ecosystem more precisely than aligning it with NodeEngine's bus.

Concrete benefits of OSC:
- Removes the bus0-doesn't-auto-relay topology problem entirely
- Removes the `controller.local` Avahi dependency (was broken on node-002)
- Removes `libnng-dev` from .deb dependencies
- Deletes ~600 lines of NngBusClient + StatusEmit infrastructure
- Lower latency (~1–2ms saved by not going through controller relay)
- Each node's daemon only handles its own fades (vs broadcast-and-filter)

## Spec implications

[specs/005-nng-bus-client/](../specs/005-nng-bus-client/) becomes superseded for the inbound transport. The motion/registry/curve specs (006, 002, 003, 004) are unchanged. I'd propose either:

1. Mark spec 005 as superseded and add a header pointer to the new OSC spec
2. Add a "Transport Decision: OSC" section to spec 005 documenting the topology finding and the OSC alternative

Whichever you prefer.

## What I'd like from you

- A read of this and the [detailed plan](phase-h-osc-refactor-plan.md)
- Any concerns or alternative ideas — especially: future features that would specifically need NNG semantics that I missed
- Heads-up on any in-flight work on the daemon that this would conflict with
- A sense of timing — would you want to do the daemon-side refactor yourself, or are you happy to have me do it on a `feat/osc-input-transport` branch (off `feat/phase5-systemd-packaging`) that you can review?

I'm not pushing to `main` on either repo without your sign-off. The cuems-engine fixes already landed are on `merge/fade-cue-staging` (off your `feat/fade-cue`). Phase H lives on new branches that don't touch protected names.

Thanks for the foundation work — the FadeMotion + MotionRegistry + curve infrastructure is genuinely well-designed and reused as-is. Only the input transport changes. The integration testing was instructive: your wire format checks (target/node_name filter, parser fail-fast) caught the engine-side bugs cleanly, and the registry's supersede semantics are exactly what we need.

— Ion
