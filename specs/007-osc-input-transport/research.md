<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Research — Phase H: OSC Input Transport

**Feature**: [007-osc-input-transport](spec.md)
**Date**: 2026-05-13
**Status**: Complete — all Technical Context unknowns resolved.

This document collects the decisions that the Phase H planning notes already pre-decided, plus the additional resolutions needed to lock the implementation contract. Anywhere the planning notes left a choice open, this document picks the option and records the rationale.

---

## Decision 1 — OSC library for the server side: **liblo (existing dependency)**

**Decision**: Use `liblo` for receiving OSC commands in the daemon, via `lo_server_thread_new` (or `lo_server_new` + a manually-driven thread). Do **not** add `oscpack` as a new build dependency.

**Rationale**:

- `liblo` is already linked into the daemon and `libgradient_motion` for the output side (`gme::osc::OscSender` calls `lo_send`). Server-mode support (`lo_server_thread`, `lo_server_add_method`) is part of the same library.
- The Phase H planning note (`specs/planning/phase-h-osc-refactor-plan.md` H.2) suggested `oscpack` to "reuse the pattern from `cuems-audioplayer/src/oscreceiver/oscreceiver.cpp`". The *pattern* (localhost UDP listener with per-address method dispatch) is library-agnostic — `liblo` implements it natively and is already in our build graph.
- Adding `oscpack` would mean one more `Build-Depends` line in `debian/control` and one more transitive dependency for embedders of `libgradient_motion`. Constitution Principle III (Library-First) prefers fewer external dependencies on the library side.
- `liblo` server-mode supports per-address method registration (`lo_server_add_method("/gradient/start_fade", "...", cb, ctx)`), bound type-tag matching, and explicit per-method context pointers — all the features the daemon needs.

**Alternatives considered**:

- `oscpack` — rejected; would add a build dependency for no functional gain.
- A hand-written UDP receiver + custom OSC parser — rejected; reinventing the wheel and creates a second OSC parser path inside the project. Maintenance hazard.
- Async I/O via `boost::asio` — rejected; pulls in Boost just for a localhost UDP listener.

**Implementation notes**:

- The receive callback runs on `liblo`'s server thread (network thread). It must be wait-free with respect to the tick thread — i.e. it pushes parsed `FadeCommand` records into a `LockFreeQueue<FadeCommand, 64>` and returns immediately. No mutex acquisition, no allocation in the hot path beyond what `nlohmann::json` does for `curve_params` (same as the existing NNG path).
- The server thread is owned by an `OscServer` wrapper class in `daemon/comms/OscServer.{cpp,h}` and joined cleanly on daemon shutdown.
- Bind address: `127.0.0.1` only. Pass `"127.0.0.1"` as the `iface` argument to `lo_server_thread_new_multicast_iface` or equivalent; for a pure UDP server, use `lo_server_thread_new()` with a port and rely on the OS-level bind to localhost via `setsockopt(IPV6_V6ONLY)` / explicit `bind()` if liblo's default is too permissive. **Validate at implementation time** that `liblo` actually binds only to loopback under the chosen API — if it does not, fall back to creating the socket manually and handing it to liblo. (Recorded as an implementation-validation task, not a spec gap.)

---

## Decision 2 — OSC address namespace and message schema

**Decision**: Use the following three addresses with fixed type tags:

```text
/gradient/start_fade   ,sssisffhisss      (motion_id, node_name, osc_host,
                                            osc_port, osc_path,
                                            start_value, end_value,
                                            start_mtc_ms, duration_ms,
                                            curve_type, curve_params_json)
/gradient/cancel_motion  ,ss              (motion_id, node_name)
/gradient/cancel_all     ,s               (node_name)
```

> **Note on `start_mtc_ms`**: tightened to OSC type `h` (int64) — see Decision 4 below.

**Rationale**:

- The address namespace `/gradient/...` is unique enough to avoid clashes with player paths like `/volmaster` / `/opacity` that are themselves OSC destinations the daemon emits to.
- The three-message set covers the contract from spec FR-003.
- `node_name` is carried on every message as defense-in-depth (FR-004). On localhost it is theoretically redundant — only the local NodeEngine can reach the local daemon's port — but it lets the daemon log a clear warning if a misconfigured deployment ever does cross-talk, and it matches the existing NNG-side filter the C++ parser already enforces.
- `curve_params` continues to travel as an embedded JSON string (`s`) rather than being unpacked into the OSC argument list. Curve parameters are heterogeneous (Bezier needs 4 floats; future spline types may need arrays) and a JSON string lets the existing `nlohmann::json` parser in `parseFadeCommand` consume them with no schema change in the OSC layer. Wire cost is negligible (~50 bytes typical).

**Alternatives considered**:

- One generic `/gradient` address with a discriminator string as first argument — rejected; loses the per-address dispatch that `liblo`'s method table gives us for free, and makes the type tag harder to validate.
- Unpacking `curve_params` into the OSC argument list — rejected; couples the wire schema to every new curve type added in feature 002's CurveFactory.
- Including a schema-version field — deferred; not needed for v1, and adding it later is a non-breaking change (extra optional trailing argument with a known type tag).

---

## Decision 3 — Crossfade dispatch is out of scope for this feature

**Decision**: This feature ships only the three messages in Decision 2. `start_crossfade` is **not** added in Phase H even though the planning notes mention it.

**Rationale**:

- Spec 007 explicitly carves Phase 7 crossfade dispatch out of scope (spec §"Out of scope for this feature").
- The OSC argument format trivially scales to a crossfade via `/gradient/start_crossfade` with a paired argument list; adding it later is purely additive and does not affect any code shipped in Phase H.
- The existing `FadeCommand::Type::START_CROSSFADE` enum and `partner_*` fields in `src/signal/FadeCommand.h` are kept as-is. They sit dormant until Phase 7.

**Alternative**: include the crossfade address now. Rejected — increases test surface and review surface in a feature whose primary goal is the transport flip, not new functionality.

---

## Decision 4 — `start_mtc_ms` is int64 (`h`), not int32 (`i`)

**Decision**: Use OSC type tag `h` (int64) for `start_mtc_ms` in `/gradient/start_fade`. The Phase H planning note's draft schema `i` is tightened.

**Rationale**:

- The C++ side's `FadeCommand.start_mtc_ms` is `uint64_t` (confirmed against feature 005's `FadeCommand.h` contract: required field documented without an explicit width, but the in-memory representation chosen at Phase 3 was 64-bit unsigned to absorb the 24-hour MTC rollover counter that cuems-engine threads through `frozen_mtc_ms`).
- A 24-hour MTC range fits inside int32 (~86.4 M ms), so int32 is technically sufficient for *one* day. But cuems-engine deals with MTC absolute timestamps that can exceed 24h (rc6 added rollover-aware timecode; the value is monotonic across the engine's runtime). Choosing int64 on the wire matches the in-memory width and removes a future overflow trap.
- All other ms fields (`duration_ms`) stay int32 (`i`). A fade longer than 24 days is implausible and the existing FadeMotion accepts `uint32_t` for duration.

**Alternative**: keep `i` matching the draft. Rejected as a foot-gun for the planned MTC rollover handling.

---

## Decision 5 — Daemon CLI flag changes

**Decision**:

- **Delete** `--nng-url <url>`.
- **Add** `--osc-port <port>` (default `7100`).
- **Add** environment-variable override `CUEMS_GRADIENT_OSC_PORT` consulted only if `--osc-port` is not given.
- **Keep** `--node-name <name>` (already present; still required for the filter in FR-004).
- **Keep** the existing config-file mechanism (Phase 3 daemon reads from `/etc/cuems/settings.xml`'s `<gradient_osc_port>` element via the existing `Config` class) and document the resolution order: CLI flag > env var > settings.xml > built-in default 7100.

**Rationale**:

- Single source of truth for the port. The systemd unit can pass `--osc-port 7100` explicitly so the operator sees the live value in `systemctl cat`, or omit it to let settings.xml win.
- Matches the existing `<oscquery_osc_port>` convention used by cuems-engine for other player ports — operators already know this pattern.

**Alternative**: require the port to come exclusively from settings.xml (no CLI flag). Rejected — breaks local development where running the daemon with a one-off port (e.g. while another instance holds 7100) is useful.

---

## Decision 6 — Status emission is removed wholesale, not refactored

**Decision**:

- **Delete** `src/signal/StatusEmitRequest.h`.
- **Delete** the status-emit queue and any references from `GradientEngine`.
- **Delete** the `motion_complete` / `motion_error` paths from the daemon entirely. No replacement.
- All operationally-relevant signals (parse error, OSC send failure, registry supersede) are emitted to the daemon's local logger (`cuemslogger` → syslog → `journalctl -u cuems-gradient-motiond`) with a structured log line.

**Rationale**:

- Spec FR-008 forbids network status. The status queue exists *only* to feed network status. Keeping it as dead code (for "maybe future use") violates Constitution Principle III (Library-First — the library should not carry dead infrastructure).
- Future fade-progress UI work lives in cuems-engine's `loop_fadeCue`, not in the daemon (spec §"User Story 4", FR-015).
- Local logs are visible to operators via journalctl and to developers via `cuems-gradient-motiond --log-level debug` (existing CLI option). No UX regression.

**Alternative**: keep `StatusEmitRequest` and the queue, just disable the network emit. Rejected — leaves a confusing half-feature.

---

## Decision 7 — `LockFreeQueue` and `FadeCommand` keep their place

**Decision**:

- `src/signal/LockFreeQueue.h` is **retained, unchanged**. It is reused as the OSC-server-thread → tick-thread handoff (matching its role in the NNG era).
- `src/signal/FadeCommand.{h,cpp}` are **retained**. The struct is unchanged. The JSON parser stays in place to keep existing unit tests green during the transition, and is then *removed* once the OSC parse path is fully wired (see plan phase 2 task ordering).
- A new free function `parseFadeOscCommand` in `src/signal/parseFadeOscCommand.{h,cpp}` is added. It accepts a `lo_arg**` + count + type-tag string + this-node-name and returns the same `ParseResult` enum the JSON parser returns. Reusing the enum keeps existing error-handling in `GradientEngine` working with minimal change.

**Rationale**:

- Constitution Principle II (Modular Architecture) — `gme::signal` owns the command record. The transport layer (`daemon/comms`) calls into `gme::signal` to parse; it does not own the parse logic.
- Reusing `LockFreeQueue` preserves Constitution Principle IV (Real-Time Safety) — the queue is the proven mechanism for crossing the network-thread / tick-thread boundary without locks.
- Reusing `ParseResult` keeps `GradientEngine::drainCommandQueue` (or equivalent) shape-compatible.

**Alternative**: define a separate `OscFadeCommand` struct. Rejected — duplicate state, no clean reason to diverge.

---

## Decision 8 — Tests: parse-level + integration replacement

**Decision**:

- **Delete** `tests/test_nng_integration.cpp` and `tests/test_nng_parse.cpp`.
- **Add** `tests/test_osc_parse.cpp` covering:
  - Well-formed `/gradient/start_fade` → `FadeCommand{Type::START_FADE, …}`.
  - Well-formed `/gradient/cancel_motion` → `FadeCommand{Type::CANCEL_FADE, motion_id, …}`.
  - Well-formed `/gradient/cancel_all` → `FadeCommand{Type::CANCEL_ALL, …}`.
  - Type-tag mismatch (e.g. `start_fade` with `,sssisffffss` instead of `,sssisffhisss`) → `TypeError`.
  - Wrong argument count → `MissingField`.
  - `node_name` mismatch → `NodeMismatch` (silently dropped).
  - Malformed `curve_params_json` → `TypeError` with `motion_id` preserved in error path so the logger can name the offending fade.
  - Unknown keys inside `curve_params_json` → silently ignored (forward-compatibility, mirrors existing FR-014 from spec 005).
- **Add** `tests/test_osc_server_integration.cpp`: spins up an `OscServer` on a randomly-chosen free port, sends a well-formed `/gradient/start_fade` via `liblo`'s client side, asserts the queued `FadeCommand` matches.
- Other tests (`test_motion_registry`, `test_fade_motion`, `test_curves`, `test_lockfree_queue`, `test_mtc_tick_source`) — **untouched**.

**Rationale**:

- Phase H promises "all previously green tests stay green" (spec SC-006). The motion-side tests have no transport dependency, so they pass unchanged.
- Parse-level tests are cheap and high-leverage — they cover the field-by-field contract.
- One integration test proves the network-thread → queue handoff actually works end-to-end inside the process. Anything beyond that (engine + tick + OSC out) is covered by the existing quickstart smoke and by the on-hardware verification in spec SC-001.

**Alternative**: skip parse-level tests, lean entirely on the integration test. Rejected — integration tests are slower and noisier to diagnose.

---

## Decision 9 — Debian packaging

**Decision**:

- `debian/control` Build-Depends: **drop** `libnng-dev`. liblo-dev stays (already required for the sender side).
- `debian/control` Depends (runtime): **drop** `libnng1` (or whatever NNG runtime SO version the current package depends on). `liblo7` (or current SO version) stays.
- The systemd unit `cuems-gradient-motiond.service` lives in `cuems-common`, not this repo. The packaging change there is tracked by the corresponding cuems-engine-side branch (`feat/gradient-osc-transport`). The C++ side delivers the daemon binary; the system integration is rolled in the deb produced from this branch.

**Rationale**: Spec FR-016 requires no libnng runtime dependency. The packaging skeleton is the enforcement point.

**Alternative**: leave libnng in the package as "transitional". Rejected — Spec SC-004 is explicit.

---

## Decision 10 — Build-time gate that prevents NNG creeping back in

**Decision**: After the NNG client deletion lands, **remove** the `find_package(nng REQUIRED)` call from the top-level `CMakeLists.txt`. Replace with no NNG mention at all. A `grep -r nng` over `src/`, `daemon/`, `tests/` should return zero hits except in `dev/` historical notes and `specs/005-*` (which is now superseded but kept for history).

**Rationale**: Prevents a reviewer or a future merge from silently re-introducing the dependency. Constitution Principle V (Protocol-Agnostic Core) is reinforced.

---

## Open implementation-validation items (not spec gaps)

These are decisions that are locked at the spec level but require verification at implementation time. They are not unresolved — they are checklist items for the implementer.

1. **Confirm `liblo` server-mode bind is restricted to `127.0.0.1`** under the chosen API. If not, build the UDP socket manually and attach it.
2. **Confirm `liblo`'s server thread is signal-safe** with respect to SIGTERM cleanup (`lo_server_thread_free` joins the thread). If `lo_server_thread_free` blocks, switch to a manually-pumped `lo_server` running in a `std::thread` we own.
3. **Measure** the per-command latency from `lo_send` (Python side, NodeEngine) to the daemon's queue push, to verify spec SC-002 (< 1 ms). The existing `gettimeofday`-based timing harness in `dev/` is adequate.
4. **Verify** the daemon binary has no libnng dynamic-link dependency after the cleanup (`ldd build/gradient-motiond | grep -i nng` → empty), per spec SC-004.

---

## Constitution alignment cross-check

- **Principle I (Deterministic Evaluation)** — unaffected. Evaluation reads from the lock-free queue; the queue's producer side changed from NNG-thread to liblo-thread but the contents are identical (`FadeCommand`).
- **Principle II (Modular Architecture)** — improved. `gme::signal` keeps `FadeCommand` and `LockFreeQueue`; transport layer (`daemon/comms`) shrinks from NNG to OSC; no new cross-module coupling.
- **Principle III (Library-First)** — improved. NNG dependency leaves `libgradient_motion`'s transitive closure entirely (it was a daemon-side dep already, but the daemon's footprint shrinks).
- **Principle IV (Real-Time Safety)** — preserved. liblo callback uses the same lock-free queue handoff. No new allocation in the tick path.
- **Principle V (Protocol-Agnostic Core)** — strengthened. The library carries no NNG references at all after the cleanup.
- **Principle VI (Documentation Standards)** — applies to new headers (`OscServer.h`, `parseFadeOscCommand.h`). Both will ship with complete docstrings per contracts.
- **Principle VII (Extensibility via Abstraction)** — unaffected. Motion / curve / registry hierarchies are not touched.
