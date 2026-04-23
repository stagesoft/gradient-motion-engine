# Contract: `gme::motion::FadeMotion`

**Files**: `src/motion/FadeMotion.h/.cpp`  
**Namespace**: `gme::motion`  
**Supertype**: `gme::motion::IMotion`

For lifecycle, threading, and status-emission contracts shared by all motion types, see [`motion-registry-api.md`](motion-registry-api.md).

---

## Scalar-fade-specific state

| Field | Description |
|-------|-------------|
| `start_value` | OSC float value at t=0. May be overwritten by `inheritFrom` on supersede. |
| `end_value` | OSC float value at t=1. Set at construction; immutable thereafter. |
| `last_sent_value` | Most recent float value sent via OSC. Updated every tick by `evalAndSend`. Used as the inherited start position on supersede. |
| `osc_path` | liblo OSC destination path, e.g. `"/volmaster"`. |
| `osc_host` | Host string (stored for diagnostics; the pre-built `lo_address` is used for sends). |
| `osc_port` | UDP port (stored for diagnostics). |

---

## `FadeMotion::evalAndSend(long mtc_ms)` → `EvalResult`

1. Compute `t`:
   - If `duration_ms == 0`: `t = 1.0` (instant completion).
   - Else: `t = clamp((mtc_ms − start_mtc_ms) / duration_ms, 0.0, 1.0)`.
2. Evaluate curve: `curve_val = curve->evaluate(t)`.
3. Interpolate: `value = start_value + (end_value − start_value) * curve_val`.
4. Send: `ret = oscSend_(osc_target, osc_path.c_str(), value)`.
5. Update: `last_sent_value = value`.
6. Return `EvalResult { completed = (t >= 1.0), failed = (ret != 0), failure_reason = (ret != 0) ? "osc_send_failed" : nullptr }`.

`consecutive_osc_failures` is **not** incremented inside `evalAndSend`; it is managed by `MotionRegistry::tick` based on `EvalResult::failed`.

**Throws**: Never. `oscSend_` is required to be noexcept-equivalent.

---

## `FadeMotion::sendSnapToEnd()`

Calls `oscSend_(osc_target, osc_path.c_str(), end_value)` once. Called only by `MotionRegistry::cancelMotion(snap_to_end=true)`. Does not update `last_sent_value` (the motion is removed immediately after).

**Throws**: Never.

---

## `FadeMotion::inheritFrom(const IMotion& prior)`

Copies the outgoing fade's last OSC position so the new fade begins seamlessly:

```
if (auto* fm = dynamic_cast<const FadeMotion*>(&prior)) {
    this->start_value     = fm->last_sent_value;
    this->last_sent_value = fm->last_sent_value;
}
// Type mismatch (e.g. VectorMotion<3> → FadeMotion): no-op.
```

This prevents a visible jump in the OSC stream when a new fade supersedes a running fade mid-progress.

**Throws**: Never.

---

## Construction (via `MotionFactory::fromCommand`)

`FadeMotion` is constructed exclusively by `MotionFactory`. The factory:

1. Calls `CurveFactory::createCurve(cmd.curve_type, params)`. On `nullopt`: emits `MotionError:"unknown_curve_type"`, returns `nullptr`.
2. Resolves `start_mtc_ms == -1` → `mtcSource.getMtcMs()`.
3. Builds `osc_key = host + ":" + port + ":" + path`.
4. Calls `gme::osc::makeAddress(host, port)`. On `nullptr`: emits `MotionError:"osc_address_failed"`, returns `nullptr`.
5. Constructs and returns `std::make_unique<FadeMotion>(...)`.

The registry's `addMotion` only ever sees a fully-constructed, non-null motion when construction succeeds.

---

## Destructor

Calls `lo_address_free(osc_target_)`. The curve is freed via `std::unique_ptr<Curve>` RAII. No registry involvement needed on destruction.
