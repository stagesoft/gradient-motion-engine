# Phase 1 Data Model: Adapt MtcTickSource to mtcreceiver v2.0.0

**Feature**: 004-adapt-mtc-tick-v2
**Date**: 2026-04-22

This feature does not introduce new entities or persistent data. It adapts a
single existing component (`MtcTickSource`) to a new upstream callback
contract. The "data model" for this change is therefore a behavioural /
lifecycle model rather than a schema.

## Entities

### MtcTickSource (existing — behavioural changes only)

| Attribute | Type | Notes |
|-----------|------|-------|
| `receiver_` | `std::unique_ptr<MtcReceiver>` | Unchanged. Null until `start()` succeeds. |
| *(internal)* consumer callback | *captured by the adapter lambda registered via `MtcReceiver::setTickCallback`* | No longer stored as a member. The adapter closure owns it. |

**Invariants**:

- At most one consumer callback is registered in `MtcReceiver` at any given
  moment, regardless of how many times `setTickCallback()` is called (v2.0.0
  enforces a single slot with internal mutex).
- After the destructor returns, no consumer callback invocation may occur —
  `MtcReceiver::setTickCallback({})` blocks until any in-flight invocation
  completes (per v2.0.0 contract).

**State transitions** (high-level lifecycle):

```text
Constructed
    │
    ├── setTickCallback(cb)         ──► [Callback registered]
    │       │
    │       ├── setTickCallback({}) ──► [No callback]
    │       │
    │       └── setTickCallback(cb2)──► [Callback registered (cb2 replaces cb)]
    │
    ├── start(portName)
    │       │
    │       ├── OK                  ──► [Running — ticks dispatched]
    │       ├── kNoPortsAvailable   ──► [Idle (no port opened)]
    │       └── kPortNotFound       ──► [Idle (no port opened)]
    │
    └── ~MtcTickSource()
            │
            └── setTickCallback({}) ──► [Deregistered — guaranteed no-call-after-dtor]
                    │
                    └── receiver_ release (if any) ──► [Destroyed]
```

### MtcReceiver v2.0.0 (external — shape only, not under this feature's control)

Documented here for reference; changes live upstream.

| Symbol | Signature / Type | Change vs v1 |
|--------|------------------|---------------|
| `setTickCallback` | `static void setTickCallback(TickCallback)` | **NEW** (replaces public `onQuarterFrame`). |
| `TickCallback` alias | `std::function<void(long mtcHeadMs, bool isCompleteFrame)>` | **NEW** signature (added `bool`). |
| `onQuarterFrame` | *(removed)* | **REMOVED**. |
| `invokeTickForTesting` | `static void invokeTickForTesting(long ms, bool isCompleteFrame)` | **NEW** (only under `-DMTCRECV_TESTING`). |
| `SkipPortOpenTag` | constructor tag type | **NEW** (only under `-DMTCRECV_TESTING`). |
| `decodeQuarterFrameForTesting` | static helper | **NEW** (only under `-DMTCRECV_TESTING`). |
| `resetDecoderStateForTesting` | static helper | **NEW** (only under `-DMTCRECV_TESTING`). |
| `resetStaticStateForTesting` | static helper | **NEW** (only under `-DMTCRECV_TESTING`). |

## Relationships

- `MtcTickSource` adapts (1 → 1) the consumer's
  `std::function<void(long)>` to mtcreceiver's
  `std::function<void(long, bool)>`. The adapter lambda is the only stored
  reference to the consumer callback; its lifetime is tied to
  `MtcReceiver`'s internal slot, not to `MtcTickSource`.

## Validation rules

- `setTickCallback(std::function<void(long)> cb)`:
  - If `cb` is a truthy callable: register an adapter lambda that invokes
    `cb(ms)` and discards `isCompleteFrame`.
  - If `cb` is empty/null: register `{}` (empty function), i.e. deregister.
- Destructor: unconditionally deregister before releasing `receiver_`.
- No exceptions may escape the adapter lambda — it is invoked from the
  MIDI thread; propagating an exception would cross a thread boundary and
  violate both mtcreceiver's contract and this project's constitution
  (Performance & Safety Standards).
