# Research: Phase 4 — Fade Registry, Tick Loop & OSC Sender

**Branch**: `006-fade-registry-tick-loop` | **Date**: 2026-04-23

## Decision 1: Status Worker Producer Model

**Question**: The spec says `NngBusClient` owns the status worker thread and SPSC queue. But both the MTC tick thread (fade_complete, osc_send_failed) and the fallback drain thread (fade_error:superseded from addFade) need to emit status. An SPSC queue has exactly one producer. How do we reconcile this?

**Decision**: Two-tier status emission model:
- **MTC tick thread → SPSC status queue → status worker thread**: Handles `fade_complete` and `fade_error:osc_send_failed`. These are performance-critical paths that must not block the real-time tick.
- **Fallback drain thread → direct `sendStatus()`**: The fallback drain thread has no real-time budget (it fires every 100 ms). When `addFade` is called from the fallback drain thread and a supersede occurs, the drain thread calls `NngBusClient::sendStatus()` directly. This is safe because `sendStatus` is thread-safe (FR-007, NNG socket under lock, verified in Phase 3 research Decision 5).

**Rationale**: Preserves SPSC invariant on the status queue (single producer = tick thread). Avoids the complexity of a multi-producer queue for a low-frequency path. The fallback drain context is never time-critical.

**Alternatives considered**:
- MPSC queue for status: Would require a CAS loop on push. Adds complexity and slightly higher latency on the hot path for a rare event (supersede from drain thread).
- Always route through status queue: Would require either MPSC or a mutex — both Principle IV violations if the tick thread is the producer.

---

## Decision 2: `lo_send` Real-Time Safety on Loopback

**Question**: `lo_send` performs a UDP `sendmsg(2)` syscall. Is this safe to call from the MTC tick thread without violating Principle IV (Real-Time Safety)?

**Decision**: `lo_send` on loopback is acceptable in the tick thread under documented assumptions.

**Rationale**:
- `sendmsg(2)` to a loopback UDP socket on Linux returns immediately when the kernel socket send buffer is not full. For OSC fade messages (~60 bytes), the send buffer (~212 KB default) can hold ~3500 messages. At 50 fades × 200 Hz × 60 bytes = 600 KB/s total throughput — well below loopback capacity (limited by kernel, typically >1 GB/s).
- The kernel call takes ~1–5 µs per call. At 50 fades: 50 × 5 µs = 250 µs worst case per tick, within the 2 ms budget (SC-007).
- If a receiving process is slow to consume (e.g., AudioPlayer is hung), the UDP packets are dropped by the kernel — `sendmsg` does NOT block. UDP is fire-and-forget.

**Documented assumption**: AudioPlayer and VideoComposer are always on localhost. If a remote OSC target were introduced, `lo_send` could block on network I/O and this decision would need revisiting.

**Alternatives considered**:
- Move lo_send to a per-fade output thread: Eliminates the syscall from the tick thread but adds a staging queue per active fade and introduces additional latency before the first OSC send. Overly complex for a loopback-only use case.

---

## Decision 3: `FadeRegistry` Data Structure

**Question**: What data structure should `FadeRegistry` use for the primary fade map and supersede detection?

**Decision**:
- **Primary index**: `std::unordered_map<std::string, std::unique_ptr<ActiveFade>>` keyed by `fade_id`. Pointer stability after insertion is guaranteed by the C++ standard (unordered_map does not invalidate pointers/references on rehash — only on erase). This ensures the Phase 7 `crossfade_partner` raw pointer remains valid as long as the pointed-to `ActiveFade` is in the map.
- **Secondary index**: `std::unordered_map<std::string, std::string>` keyed by OSC composite key `"host:port:path"` → `fade_id`. Used only in `addFade` and `cancelFade` to detect/clear superseded entries (FR-014). Never touched during `tick()`.

**Rationale**:
- `unordered_map::find` is O(1) amortized with no allocation. `tick()` iterates by value over the primary map — standard range-for over the map is safe since tick() never inserts or erases (only marks `completed = true`; removal happens after `tick()` returns).
- Rehashing only occurs on insert (`addFade`), not during `tick()`. The steady-state tick path is allocation-free per Principle IV.
- `unique_ptr<ActiveFade>` keeps `ActiveFade` heap-allocated with stable addresses, safe for Phase 7 partner pointer.

**Alternatives considered**:
- Fixed-size array of `ActiveFade` by value: O(N) lookup, requires an ID→index secondary map, max-fade cap baked into the type. Simpler for small N but less ergonomic and harder to grow.
- `std::map<std::string, ActiveFade>`: O(log N) lookup, allocates per node, no pointer stability guarantee on erase. Worse on both counts.

---

## Decision 4: `CurveFactory` Unknown-Type Handling in `addFade`

**Question**: `CurveFactory::createCurve` returns `std::nullopt` for unknown curve types. What should `FadeRegistry::addFade` do?

**Decision**: On `nullopt` from `CurveFactory`, `addFade` logs a warning, enqueues a `fade_error` status with `reason: "unknown_curve_type"` on the status queue (or calls `sendStatus` directly if called from the drain thread), and returns without registering an `ActiveFade`.

**Rationale**: The fade_id is known at this point (it's in the `FadeCommand`). Emitting a `fade_error` lets the Controller know the fade was rejected and can take corrective action. Silently dropping would leave the Controller waiting for a `fade_complete` that never arrives.

**Fallback policy**: No implicit fallback to `LinearCurve`. The `CurveFactory` docstring explicitly notes that returning `nullopt` makes the error observable so the caller can apply the appropriate policy. The registry's policy is: reject with error.

---

## Decision 5: OSC Failure Threshold and Counter Reset

**Question**: FR-006a says "consecutive failures exceed a defined threshold." What counts as a failure, and when does the counter reset?

**Decision**:
- **Failure**: Any non-zero return from `lo_send` (liblo returns 0 on success, negative errno on error).
- **Threshold**: `kOscFailureThreshold = 5` consecutive failures (compile-time constant in `FadeRegistry.h`).
- **Counter reset**: Reset to 0 on any successful `lo_send`. Consecutive failures must be uninterrupted.
- **On threshold breach**: Enqueue `fade_error:osc_send_failed`, remove the fade from both primary and secondary indexes. The fade is gone — no further ticking.

**Rationale**: 5 consecutive failures at 200 Hz = 25 ms of silence before declaring the fade dead. This is long enough to survive a momentary jitter (e.g., AudioPlayer GC pause) without generating false alerts, but short enough that the Controller gets notified quickly if the player has truly crashed.

---

## Decision 6: `OscSender` API Shape

**Question**: Should `OscSender` be a class (with state) or a free-function namespace?

**Decision**: `OscSender` is a header-only free-function in `gme::osc` namespace. The function signature:

```cpp
namespace gme::osc {
    // Returns lo return code (0 = success, negative = error).
    // lo_address is owned and managed by the caller (ActiveFade).
    int sendFloat(lo_address target, const char* path, float value) noexcept;
}
```

**Rationale**: There is no per-sender state — `lo_address` lifetime is owned by `ActiveFade`, and `lo_send` is a free function in liblo. Wrapping in a class would add unnecessary indirection. The `noexcept` guarantee satisfies Principle IV (no exception propagation through the library).

**Alternatives considered**:
- Class with `lo_address` member: Puts OSC address management in `OscSender`. But `ActiveFade` needs to own the address for lifetime management tied to the fade's lifecycle. This would require passing the sender through the fade, coupling the two.

---

## Decision 7: `GradientEngine` Tick-Thread Implementation

**Question**: How does `GradientEngine` implement the tick thread without a free-running loop?

**Decision**: `GradientEngine` has no dedicated "tick thread". The tick fires from the MTC callback (registered via `MtcTickSource::setTickCallback`). The callback runs on the RtMidi MIDI thread (managed by `mtcreceiver`). `GradientEngine::onTick(long mtc_ms)` is called directly from that thread:

1. Try-acquire `drain_in_progress_` on `NngBusClient` (to serialise with fallback drain).
2. If acquired: drain `LockFreeQueue` → `FadeRegistry::apply(cmd)` per command. Release flag.
3. `FadeRegistry::tick(mtc_ms)` — evaluate + send OSC.
4. Remove completed fades from registry.

The fallback drain thread (owned by `NngBusClient`, 100 ms timer) also calls `drainOnce()` which tries the same `drain_in_progress_` flag. If the MTC tick is currently draining, the fallback returns immediately and retries on the next 100 ms interval.

**Rationale**: This is the design established in Phase 3. The "MTC tick thread" is the RtMidi callback thread — not a new thread. `GradientEngine` simply registers `onTick` as the callback and manages the registry.
