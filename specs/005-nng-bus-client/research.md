# Phase 0 Research: Phase 3 — NNG Bus Client, FadeCommand & LockFreeQueue

**Feature**: 005-nng-bus-client
**Date**: 2026-04-22

All Technical Context entries in `plan.md` are fully resolved — no
`NEEDS CLARIFICATION` markers remain. This document records the decisions
that anchor the Phase 3 implementation and the alternatives that were
weighed.

## Decision 1 — NNG socket role and API surface

- **Decision**: Use the NNG C API directly (`#include <nng/nng.h>` and
  `<nng/protocol/bus0/bus.h>`). Open a BUS0 socket with
  `nng_bus0_open(&sock)`. Dial the controller URL with
  `nng_dial(sock, url, nullptr, NNG_FLAG_NONBLOCK)`. Set the socket reconnect
  parameters with `nng_socket_set_ms(sock, NNG_OPT_RECONNMINT, 1000)` and
  `NNG_OPT_RECONNMAXT, 30000)` **before** the `nng_dial` call. Run a
  dedicated receive thread that calls `nng_recv(sock, &buf, &sz, NNG_FLAG_ALLOC)`
  in a loop; free each buffer with `nng_free(buf, sz)` after parsing. Send
  status messages from the tick/shutdown thread via `nng_send(sock, data, sz,
  0)` (blocking send is acceptable; status volume is ≤ 1 msg per fade
  completion).
- **Rationale**: BUS0 is what every other CUEMS peer on the hub speaks
  (confirmed by the Python `HubServices.py:217` reference call in the
  implementation plan). Non-blocking dial is the only way to keep daemon
  startup independent of hub availability — required by FR-001. The
  1 s / 30 s reconnect bounds match pynng's behaviour so the Controller sees
  consistent reconnect cadence regardless of peer implementation.
  `NNG_FLAG_ALLOC` avoids a fixed-size receive buffer; NNG owns the
  allocation and hands the daemon a sized view. Using the C API (not NNG's
  C++ wrapper) matches the style of the rest of the CUEMS C++ codebase and
  avoids pulling in an optional dependency.
- **Alternatives considered**:
  - **nngpp C++ wrapper** — rejected. It is header-only but introduces
    RAII exceptions through the socket API (violates Principle IV:
    "Exceptions MUST NOT cross library boundaries"). It also pins the
    daemon to whatever version the submodule carries, which is friction
    for Debian packaging.
  - **Reuse pynng semantics via a subprocess** — rejected, defeats the
    purpose of a native daemon (localhost zero-jitter OSC, Principle IV).
  - **REQ/REP or PUB/SUB instead of BUS** — rejected, topology
    mismatch with the rest of the CUEMS ecosystem (NodeEngine, Controller,
    and NodesHub all speak BUS0).

## Decision 2 — JSON envelope shape and target/node filtering

- **Decision**: Parse every incoming frame with `nlohmann::json::parse(...,
  nullptr, /*allow_exceptions=*/false)`. If parsing fails, discard and log a
  `warning` (FR-012). Check two string fields **in this order** before any
  further work:
  1. `target == "gradientengine"` — if not, drop silently (FR-003).
     `gradientengine` is the uniform inbound target; no other target value
     is meaningful to this daemon.
  2. `data.node_name == <daemon config node_name>` — if missing or
     mismatched, drop silently (FR-003a).
  Only after both checks pass does the client inspect `data.command` and
  dispatch to the appropriate parser. Fade command names today are
  `start_fade` / `cancel_fade` / `cancel_all` / `start_crossfade`; any
  unknown `data.command` value (potentially a future non-fade message
  routed to the same target) emits a warning but does not emit a
  `fade_error` (there is no `fade_id` context to attribute the error to).
- **Rationale**: Two-stage filter minimises work on broadcast messages
  destined for other subsystems (NodeEngine handles most traffic) and for
  other nodes (bus0 is broadcast — every node sees every message). Short-
  circuiting on `target` first avoids any `data.*` access on messages we
  never needed to parse. Using `allow_exceptions=false` ensures no exception
  ever crosses the NNG thread boundary — nlohmann returns a sentinel
  `json::value_t::discarded` that the client inspects explicitly.
- **Alternatives considered**:
  - **Filter on `node_name` first, target second** — rejected. Most
    bus traffic is `target=nodeengine`, not `target=gradientengine`;
    filtering on target first drops ~90 % of traffic before touching
    `data`.
  - **Use JSON Schema validation** — rejected. nlohmann-json-schema is
    not packaged for Debian/Ubuntu by default, and FR-014 already mandates
    forward-compat "ignore unknown fields" semantics, which hand-rolled
    field reads naturally provide.

## Decision 3 — `LockFreeQueue<T, N>` implementation

- **Decision**: Template class in `src/signal/LockFreeQueue.h` with a
  compile-time capacity `N` (default 64). Storage is
  `std::array<T, N> buffer_` (in-class, no heap). Two `std::atomic<size_t>`
  head (write) and tail (read) indices, modulo `N`. SPSC semantics:
  - `push(T&& item)` — producer-only. If the queue is full (`(head + 1) % N ==
    tail`), the producer advances `tail` past one slot (drop-oldest) using a
    `compare_exchange_strong` loop, then writes `item` into `buffer_[head]`
    and publishes by storing `head = (head + 1) % N` with `memory_order_release`.
    Returns `false` if a drop occurred (so the caller can log), `true`
    otherwise.
  - `pop(T& out)` — consumer-only. Loads `head` with `memory_order_acquire`.
    If `head == tail`, returns `false` (empty). Otherwise reads
    `buffer_[tail]` by move into `out`, publishes
    `tail = (tail + 1) % N` with `memory_order_release`, returns `true`.
  - `size()` / `empty()` are advisory (both indices read non-atomically for
    diagnostics; not safe for flow control).
  Acquire/release pairing means the consumer's read of `buffer_[tail]`
  "happens-after" the producer's write of the same slot.
- **Rationale**: A fixed-size SPSC ring with atomic head/tail is a
  textbook construction (see Fedor Pikus, "The C++ Lock-Free Ring Buffer").
  Requires no allocations, no locks, and has wait-free consumer reads. The
  drop-oldest variant is implemented by advancing `tail` on the producer
  side when full — this breaks strict SPSC for a single CAS (the producer
  momentarily touches the consumer-owned `tail`), but since the producer
  only advances `tail` when it observed a full queue, the race is bounded
  and the CAS retries cleanly if the consumer pops in between.
  `memory_order_acq_rel` is the minimum that keeps the data payload visible
  across threads without a full `seq_cst` fence. Capacity 64 is a power of
  two which lets the compiler optimise `% N` to a mask (though code uses
  `% N` for clarity; benchmarks show the compiler does the right thing at
  `-O3`).
- **Alternatives considered**:
  - **`boost::lockfree::spsc_queue`** — rejected. Adds a Boost dependency
    to the library (`libgradient_motion`), which currently has no Boost
    exposure. Also forces heap allocation of the buffer, violating zero-
    allocation-at-steady-state (Principle IV).
  - **`moodycamel::ReaderWriterQueue`** — rejected. Single-header and
    fast, but licensed BSD with a custom attribution clause; vendoring it
    raises packaging questions. Not available as a Debian package.
  - **Fixed-size `std::deque` + `std::mutex`** — rejected. A mutex on the
    consumer path stalls the MTC tick thread if the producer holds it, even
    briefly. Violates Principle IV.
  - **Block on full (no drop-oldest)** — rejected. The NNG receive thread
    would stall on a full queue, backing up socket reads and eventually
    causing NNG to drop frames at the transport level (worse failure mode —
    silent frame loss instead of explicit drop-oldest).
  - **Dynamic capacity (runtime-sized ring)** — rejected. Adds allocation
    at construction and makes template instantiation more invasive for no
    production value. 64 is comfortably sized for the 1–10 cmd/s expected
    load.

## Decision 4 — Fallback drain: same consumer role, not a second consumer

- **Decision**: The queue has **one logical consumer role**. When MTC is
  running, the consumer role is executed from the MTC tick callback
  (running on the RtMidi MIDI thread). When MTC is stopped, the same role
  is executed from a 100 ms timer thread (`std::thread` sleeping on a
  `std::condition_variable` with a 100 ms timeout). Mutual exclusion between
  the two execution sites is guaranteed by an `std::atomic_flag`
  `drain_in_progress_` that each site acquires with `test_and_set(acquire)`
  before popping and clears with `clear(release)` afterwards. If the flag
  is already set, the site skips its drain — the other site is already
  handling the queue this cycle.
- **Rationale**: The `LockFreeQueue` is strictly SPSC by design (Decision
  3). Introducing a second concurrent consumer would require an MPSC
  variant and significantly complicate the atomics. The flag-gated
  dispatch keeps the SPSC invariant intact: at any instant, at most one
  site is popping. The 100 ms cadence matches FR-009 and gives the
  200 ms SC-004 budget a 2× headroom. The flag is `std::atomic_flag`
  (not `bool`) because `atomic_flag::test_and_set` is the only
  standard-guaranteed lock-free atomic in C++17.
- **Alternatives considered**:
  - **Use a real SPSC queue with a secondary SPSC queue for the fallback
    path** — rejected. Two queues doubles the state and introduces
    ordering ambiguity when MTC resumes mid-cycle.
  - **Spawn the fallback drain only while MTC is stopped, teardown on
    resume** — rejected. Introduces thread lifecycle churn and a race on
    MTC transport transitions.
  - **Poll MTC state from the NNG thread and decide there** — rejected.
    The NNG thread is already the producer; making it the consumer too
    defeats the hand-off pattern and couples transport to evaluation.

## Decision 5 — Status message emission: where and when

- **Decision**: Status messages (`fade_complete`, `fade_error`) are sent by
  calling `NngBusClient::sendStatus(StatusKind kind, const std::string&
  fadeId, const std::string& reason = "")`. The method is thread-safe: it
  serialises the JSON on the calling thread, then calls `nng_send` with
  blocking semantics (status payload is small, socket buffering absorbs
  the write). Expected callers:
  - Phase 3 `NngBusClient::recvLoop` — on parse failure with a parseable
    `fade_id`, calls `sendStatus(kFadeError, parsed_id, "parse_error")`
    directly. The recv thread has no real-time budget; a blocking send
    is acceptable here.
  - **Phase 4 status-emit worker thread (NEW sub-component)** — on fade
    completion or OSC send failure, `FadeRegistry::tick` (running on the
    MTC tick thread) MUST NOT call `sendStatus` directly, since `nng_send`
    is blocking I/O and the tick thread is Principle IV real-time hot
    path. Instead, Phase 4 adds a second lightweight outbound-status
    SPSC queue inside `NngBusClient` and a dedicated worker thread that
    pops `(StatusKind, fade_id, reason)` tuples and calls `sendStatus`
    out of band. The tick thread's contribution is a single `push` on
    that outbound queue — wait-free and allocation-free (pre-sized
    `std::string` buffers or a fixed-size char buffer for `reason`).
  - Phase 5 shutdown handler — on SIGTERM with N active fades, iterates
    and calls `sendStatus(kFadeError, id, "daemon_shutdown")` for each
    directly from the shutdown thread. The tick thread is being torn
    down by that point, so the shutdown thread owns the socket with no
    competition and the blocking send is safe.
  JSON envelope:
  ```json
  {"type":"status","action":"update",
   "sender":"gradientengine_<node_name>","target":"gradientengine",
   "data":{"event":"fade_complete","fade_id":"<id>","node_name":"<node>"}}
  ```
  `fade_error` sets `"event":"fade_error"` and adds `"reason": "<string>"`
  to `data`. `target` is uniformly `"gradientengine"` regardless of the
  event kind; the fade-specific discriminator lives inside `data.event`
  so future non-fade status types can coexist under the same target.
- **Rationale**: Centralising status emission on the `NngBusClient` keeps
  the NNG/JSON knowledge in one place (Principle V). The method is
  callable from any thread because `nng_send` is thread-safe on a single
  NNG socket (documented in NNG 1.10 manual §`nng_send(3)`). Blocking
  send is acceptable *for callers outside the real-time hot path* — the
  recv thread and the shutdown thread both qualify. The MTC tick thread
  does **not** qualify (Principle IV bans blocking I/O on the evaluation
  hot path), which is why Phase 4 interposes a worker-thread queue
  between the tick caller and the blocking `nng_send`. The outbound
  queue is a low-volume, low-latency path (bounded at the rate of cue
  completions, not tick rate), so a small fixed-capacity SPSC ring is
  sufficient; it is *not* shared with the inbound command queue.
- **Alternatives considered**:
  - **Outbound SPSC queue mirror of the inbound one** — rejected.
    Doubles the queue infrastructure for a low-volume path. Also
    introduces ordering questions (must `fade_complete` for fade A land
    before the next command that reuses fade A's id?) — with a direct
    blocking send, the ordering is the JVM-style "send-before-return".
  - **Async send via `nng_aio`** — rejected for this phase. `nng_aio`
    adds completion-callback complexity without a measurable throughput
    win at 1–10 msg/s. Can be reconsidered if Phase-7-era UI progress
    streaming pushes volume up.

## Decision 6 — Config: where `node_name` comes from

- **Decision**: Add a `--node-name NAME` CLI flag to
  `ConfigurationManager`. Until the XML config parser lands (Phase 5,
  out of scope here), the CLI flag is required whenever the daemon is run
  with `BUILD_DAEMON=ON` in a multi-node context. When absent, the
  daemon falls back to `gethostname(2)` and logs an `info` line stating
  "node_name defaulted to hostname: <name>". `NngBusClient` takes the
  resolved name by `const std::string&` at construction and stores a copy.
- **Rationale**: The implementation plan (line 103) says "XML parsing is
  deferred to a later phase; Phase 0 uses CLI arguments only" — this
  decision extends that convention to Phase 3. Hostname fallback is a
  safe default for single-node development and CI, and an explicit
  `--node-name` flag is the deterministic override for systemd units and
  production packaging. Reading the config string once at construction
  (not per-message) keeps the filter hot-path allocation-free.
- **Alternatives considered**:
  - **Parse `/etc/cuems/settings.xml` now** — rejected. That is Phase 5
    scope per the implementation plan; pulling it forward would scope-
    creep this feature and block landing on tinyxml2 wiring.
  - **Derive node_name from the NNG peer URL** — rejected. The URL is
    the controller URL, not the node's; the two are unrelated in a
    multi-node topology.

## Decision 7 — Wiring into `GradientEngineApplication`

- **Decision**: Phase 3's scope stops at delivering the components and
  the unit tests. `NngBusClient` is *not yet* instantiated by
  `GradientEngineApplication`. The daemon continues to run the
  `main.cpp → initialize → run (pause loop) → shutdown` lifecycle from
  spec 001. A follow-up task in Phase 4 (or a distinct sub-task at the end
  of Phase 3) will own the actual wiring: construct the `NngBusClient` in
  `initialize`, start the receive thread in `run`, stop + drain on
  shutdown. This plan explicitly carries that wiring as a single
  task in `tasks.md` so it doesn't get lost — but the *design*
  documented here (component boundaries, thread topology, shutdown
  sequence) is settled now.
- **Rationale**: Keeping the Phase 3 change surface minimal protects
  the existing Phase 0 scaffold tests (currently green) from coupling
  failures. The components are independently testable at the library
  level (per Principle II and the Development Workflow section of the
  constitution). Wiring can happen as the first task of Phase 4 once the
  `FadeRegistry` consumer is available.
- **Alternatives considered**:
  - **Wire it now, stub the consumer to a no-op** — rejected. A no-op
    consumer adds dead code, obscures test signals, and would need to
    be removed or refactored when Phase 4 lands.

## Decision 8 — Test strategy for NNG without real sockets

- **Decision**: Unit-test `FadeCommand` parsing at the JSON layer only —
  **no NNG socket** in `test_nng_parse.cpp`. The parser is factored as a
  free function `parseFadeCommand(const nlohmann::json&, const std::string&
  ownNodeName, FadeCommand& out) -> ParseResult`. The test builds JSON
  objects directly and exercises every field + every failure mode
  (missing required field, wrong-typed field, target-mismatch, node-name-
  mismatch, unknown command, unknown field ignored, crossfade round-trip).
  Socket-level behaviour (non-blocking dial, reconnect, background thread)
  is validated as part of the Phase 4 integration harness (real NNG
  loopback), not here. `test_lockfree_queue.cpp` uses `std::thread` for
  a producer/consumer stress loop (10 000 items, SC-equivalent to AS-3 of
  User Story 2) and asserts no duplicates via a per-item sequence number.
- **Rationale**: Keeping the parser socket-free means the test suite is
  fast, deterministic, and runs on any host (no MIDI, no loopback ports
  needed). The SPSC queue has no transport dependency — it is a pure
  C++17 data structure — so a `std::thread`-based stress test is
  sufficient. TSan is the right tool for race detection and the repo
  already has a `build-tsan/` tree configured.
- **Alternatives considered**:
  - **Spin up an in-process NNG loopback (`inproc://`)** — deferred to
    Phase 4. Would add dependency on a real `libnng` presence at test
    time; keeping `test_nng_parse.cpp` parse-only lets it build with
    `BUILD_DAEMON=OFF`.
  - **Mock the NNG C functions** — rejected. The NNG C API is large and
    mocking it provides no coverage gain over testing the logical layer
    (parsing + dispatch) directly.

## Open items deferred to later phases

- **Integration test with real NNG loopback** — Phase 4.
- **XML config parsing** (tinyxml2-based `settings.xml` reader) — Phase 5.
- **Systemd unit file** (`gradient-motiond.service`) — Phase 5.
- **Controller-side `ActionHandler` changes** to emit `start_fade` —
  Phase 6a/6b.
- **Crossfade registry logic** (linking partner `ActiveFade` entries) —
  Phase 7 (we deliver the command shape now so Phase 7 needs no protocol
  changes).
