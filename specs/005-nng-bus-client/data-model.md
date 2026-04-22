# Phase 1 Data Model: Phase 3 — NNG Bus Client, FadeCommand & LockFreeQueue

**Feature**: 005-nng-bus-client
**Date**: 2026-04-22

This feature introduces two library-side entities (`FadeCommand` and
`LockFreeQueue<T, N>`) and one daemon-side component (`NngBusClient`). No
persistent storage. All data is in-memory, produced from NNG JSON frames and
consumed by the (future) `FadeRegistry` on the MTC tick thread.

## Entities

### FadeCommand (library, `gme::signal::FadeCommand`)

A structured record describing a single instruction from the Controller.
The struct is a plain aggregate — no invariants beyond its fields. It is the
sole hand-off payload between the NNG thread (producer) and the tick thread
(consumer).

| Attribute | Type | Notes |
|-----------|------|-------|
| `type` | `FadeCommand::Type` enum | One of `START_FADE`, `CANCEL_FADE`, `CANCEL_ALL`, `START_CROSSFADE`. |
| `fade_id` | `std::string` | Controller-assigned unique ID. Empty when `type == CANCEL_ALL`. |
| `node_name` | `std::string` | Target node name. Set by the parser after filter match; kept for downstream telemetry. |
| `osc_host` | `std::string` | Typically `"127.0.0.1"`. Unused for `CANCEL_FADE` / `CANCEL_ALL`. |
| `osc_port` | `int` | Unused for `CANCEL_FADE` / `CANCEL_ALL`. 0 when absent. |
| `osc_path` | `std::string` | e.g. `"/volmaster"` or `"/videocomposer/layer/3/opacity"`. Unused for cancel commands. |
| `start_value` | `float` | 0.0–1.0 gain. Unused for cancel commands. |
| `end_value` | `float` | 0.0–1.0 gain. Unused for cancel commands. |
| `duration_ms` | `float` | Fade duration. Unused for cancel commands. |
| `curve_type` | `std::string` | e.g. `"linear"`, `"sigmoid"`, `"bezier"`. Unused for cancel commands. |
| `curve_params` | `nlohmann::json` | Curve-type-specific parameters. Pass-through — interpreted by `CurveFactory` in Phase 4. Unknown keys MUST be preserved here (forward-compat). |
| `start_mtc_ms` | `long` | Absolute MTC start time in ms. **`-1` is a sentinel meaning "start at current MTC position"**. Unused for cancel commands. |
| `partner_fade_id` | `std::string` | Crossfade B-side ID. Empty except when `type == START_CROSSFADE`. |
| `partner_osc_path` | `std::string` | Crossfade B-side OSC path. Empty except for crossfade. |
| `partner_start_value` | `float` | Crossfade B-side start (0.0–1.0). |
| `partner_end_value` | `float` | Crossfade B-side end (0.0–1.0). |

**Required-field rules** (enforced by `parseFadeCommand`; missing required
fields cause rejection per FR-014):

| Command (`data.command`) | Required fields in `data` |
|--------------------------|---------------------------|
| `start_fade` | `fade_id`, `node_name`, `osc_host`, `osc_port`, `osc_path`, `start_value`, `end_value`, `duration_ms`, `curve_type`, `start_mtc_ms` |
| `cancel_fade` | `fade_id`, `node_name` |
| `cancel_all` | `node_name` |
| `start_crossfade` | All `start_fade` fields PLUS `partner_fade_id`, `partner_osc_path`, `partner_start_value`, `partner_end_value` |

`curve_params` is optional in every command — when absent, stored as
`nlohmann::json::object()` (empty JSON object).

**Wire/struct key parity**: the JSON key and the C++ struct field use
the same name — `osc_path` (and `partner_osc_path` for crossfade). An
earlier draft named the wire field `osc_address`; that rename is no
longer in effect and the Python-side emitter (Phase 6) MUST emit
`osc_path`.

### LockFreeQueue<T, N> (library, `gme::signal::LockFreeQueue`)

Fixed-capacity SPSC ring buffer. Templated so tests can instantiate
smaller queues (e.g. `N=4`) to exercise overflow; production uses
`LockFreeQueue<FadeCommand, 64>`.

| Attribute | Type | Notes |
|-----------|------|-------|
| `buffer_` | `std::array<T, N>` | In-class storage, zero heap allocation. |
| `head_` | `std::atomic<size_t>` | Producer write index. Modulo `N`. |
| `tail_` | `std::atomic<size_t>` | Consumer read index. Modulo `N`. |

**Invariants**:

- `head_` and `tail_` are both `< N`. After every operation,
  `(head_ - tail_ + N) % N` is the current occupancy (0..N-1).
- The queue is never *literally* full from the consumer's perspective:
  when the producer would fill the last slot, it first drops the oldest
  entry by advancing `tail_`. Therefore the achievable maximum occupancy
  is `N - 1` slots (one slot is reserved to distinguish empty from full).
- Consumer never touches `head_` for write; producer never touches
  `tail_` for write **except** during drop-oldest, under CAS loop.
- Acquire/release pairing ensures the consumer's read of
  `buffer_[tail]` happens-after the producer's write of that slot.

**State transitions** (queue occupancy as a function of operations):

```text
[empty] ─push(x)──► [n=1]
[n=k<N-1] ─push(x)──► [n=k+1]
[n=N-1] ─push(x)──► [n=N-1]   (drop-oldest; returns false — warning logged)

[n=k>0] ─pop(&out)──► [n=k-1]
[empty] ─pop(&out)──► [empty]  (returns false)
```

**Thread-safety contract**:

- `push(T&&)` — called **only** from the NNG receive thread. Not
  reentrant.
- `pop(T&)` — called **only** from the drain site currently holding the
  `drain_in_progress_` atomic_flag (see `NngBusClient`, Decision 4).
  Never concurrent with another `pop`.
- `size()` / `empty()` — advisory only; may observe transient
  inconsistent values. Do not use for flow control.

### NngBusClient (daemon, `daemon/comms/NngBusClient`)

Daemon subsystem that owns the NNG socket, the receive thread, and the
outbound status-send API. Holds a reference (not ownership) to a
`LockFreeQueue<FadeCommand, 64>&` provided by its owner.

| Attribute | Type | Notes |
|-----------|------|-------|
| `sock_` | `nng_socket` | Opened in `start()`, closed in `stop()`. |
| `url_` | `std::string` | Controller URL, e.g. `tcp://127.0.0.1:9093`. |
| `nodeName_` | `std::string` | Local node identity. Captured at construction. |
| `senderId_` | `std::string` | `"gradientengine_" + nodeName_`. Precomputed for outbound envelopes. |
| `queue_` | `LockFreeQueue<FadeCommand, 64>&` | Reference to owner's queue. |
| `recvThread_` | `std::thread` | Background receive loop. Joined in `stop()`. |
| `running_` | `std::atomic<bool>` | Set by `start()`, cleared by `stop()`. Read by the receive loop. |
| `connected_` | `std::atomic<bool>` | Observational; set by `nng_dial` success, cleared on disconnect event. Used by tests and logs, not by control flow. |

**Lifecycle**:

```text
Constructed
    │
    └── start(url, queue)
            │
            ├── nng_bus0_open → sock_
            ├── nng_socket_set_ms(RECONNMINT=1000, RECONNMAXT=30000)
            ├── nng_dial(sock_, url, NNG_FLAG_NONBLOCK)  [OK even if hub is down]
            ├── running_ = true
            └── recvThread_ = std::thread(&NngBusClient::recvLoop, this)
                    │
                    └── while (running_) { nng_recv → parse → push }
                          │
                          └── (errors are logged; loop continues until running_ = false)

sendStatus(kind, fadeId, reason)
    │
    └── serialize JSON envelope → nng_send(sock_, ..., 0)
        (thread-safe; may be called from NNG recv thread, tick thread, or shutdown thread)

stop()
    │
    ├── running_ = false
    ├── nng_close(sock_)           [wakes up blocked nng_recv → recvLoop exits]
    └── recvThread_.join()
```

**Invariants**:

- `sendStatus()` is safe to call from any thread while `running_` is true.
  After `stop()` returns, calling `sendStatus()` is undefined (the
  socket is closed).
- After `stop()` returns, `recvThread_` is joined — no callback can still
  be running, no push into `queue_` will happen.
- `parseFadeCommand(json, nodeName_, out)` is a pure function; it does
  not touch `sock_`, `queue_`, or any member state.

## Free function: parseFadeCommand

Pure parse helper living in `FadeCommand.h`:

```cpp
enum class ParseResult {
    Ok,                    // out is populated; caller should enqueue.
    TargetMismatch,        // Drop silently (not for gradientengine).
    NodeMismatch,          // Drop silently (not for this node).
    UnknownCommand,        // Log warning; no fade_id context for fade_error.
    MissingField,          // Log warning; if fade_id is parseable, emit fade_error.
    TypeError,             // Log warning; if fade_id is parseable, emit fade_error.
    MalformedJson,         // Log warning; no fade_id context.
};

ParseResult parseFadeCommand(const nlohmann::json& envelope,
                             const std::string& ownNodeName,
                             FadeCommand& out);
```

`parseFadeCommand` is called once per inbound frame. It performs all
field extraction, type checks, and required-field validation listed
above. On `MissingField` / `TypeError` it sets `out.fade_id` if a
parseable `fade_id` was present (so the caller can attribute a
`fade_error`).

## Free function: classifyParseOutcome

The post-parse dispatch — deciding whether to enqueue, log, and/or emit
`fade_error` for a given `ParseResult` — is extracted into a pure
decision helper so that the behaviour is unit-testable without a
real NNG socket. Living alongside `parseFadeCommand` in `FadeCommand.h`:

```cpp
enum class ParseOutcomeAction {
    Enqueue,        // ParseResult::Ok → push to queue, no log.
    DropSilent,     // TargetMismatch / NodeMismatch → no log, no status.
    LogOnly,        // MalformedJson / UnknownCommand / MissingField|TypeError
                    //   without parseable fade_id → GME_LOG_WARNING only.
    LogAndStatus,   // MissingField / TypeError with parseable fade_id →
                    //   GME_LOG_WARNING + sendStatus(FadeError, fade_id, "parse_error").
};

ParseOutcomeAction classifyParseOutcome(ParseResult result, bool hasFadeId);
```

Mapping (authoritative — tested in `test_nng_parse.cpp`):

| ParseResult       | hasFadeId=false | hasFadeId=true |
|-------------------|-----------------|----------------|
| `Ok`              | `Enqueue`       | `Enqueue`      |
| `TargetMismatch`  | `DropSilent`    | `DropSilent`   |
| `NodeMismatch`    | `DropSilent`    | `DropSilent`   |
| `UnknownCommand`  | `LogOnly`       | `LogOnly`      |
| `MissingField`    | `LogOnly`       | `LogAndStatus` |
| `TypeError`       | `LogOnly`       | `LogAndStatus` |
| `MalformedJson`   | `LogOnly`       | `LogOnly`      |

`NngBusClient::recvLoop` consumes this result directly: given the
`ParseOutcomeAction`, it performs the enumerated side-effect(s) and nothing
else. The dispatch is the only non-obvious logic in `recvLoop`, so pulling
it out lets the 28-row decision table above be tested exhaustively with no
socket, no thread, and no mocks.

## External entities referenced (not under this feature's control)

- **`nng_socket`** — NNG C API opaque handle. Freed by `nng_close`.
- **`nlohmann::json`** — header-only JSON DOM. Used for parsing and
  building status envelopes.
- **Controller NodeOperation JSON envelope** — the external protocol
  shape. This feature consumes and produces it but does not define it;
  the canonical definition lives in `cuems-engine`'s `NodeCommunications.py`.
