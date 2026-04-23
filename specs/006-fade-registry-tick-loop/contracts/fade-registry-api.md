# Contract: `gme::engine::FadeRegistry`

**File**: `src/engine/FadeRegistry.h/.cpp`  
**Namespace**: `gme::engine`  
**Thread model**: All public methods must be called from a single thread at a time (tick thread or fallback drain thread, serialised externally via `drain_in_progress_` in NngBusClient).

---

## `addFade(const gme::signal::FadeCommand& cmd)`

**Brief**: Register a new fade from a parsed `FadeCommand`.

**Preconditions**:
- `cmd.type` is `START_FADE` (crossfade commands are logged and dropped in Phase 4).
- Called from the tick or drain thread context.

**Behaviour**:
1. Call `CurveFactory::createCurve(cmd.curve_type, cmd.curve_params)`.
   - On `nullopt`: push `FadeError:"unknown_curve_type"` (status queue if tick thread, direct sendStatus if drain thread). Return.
2. Resolve `cmd.start_mtc_ms`: if `== -1`, replace with `mtcSource_.getMtcMs()`.
3. Compute OSC supersede key: `"host:port:path"` from `cmd.osc_host`, `cmd.osc_port`, `cmd.osc_path`.
4. If `osc_index_` already has this key:
   - Retrieve the existing `fade_id`.
   - Erase from `fades_` (calls `lo_address_free`, `~Curve`).
   - Erase from `osc_index_`.
   - Push `FadeError:"superseded"` for the old fade_id.
5. Call `lo_address_new(cmd.osc_host.c_str(), std::to_string(cmd.osc_port).c_str())`.
   - On failure (`nullptr`): push `FadeError:"osc_address_failed"`. Return.
6. Construct `ActiveFade` with all resolved fields; insert into `fades_[cmd.fade_id]` and `osc_index_[key]`.

**Returns**: void. Errors are signalled via status queue or direct sendStatus (see above).

**Throws**: Never.

---

## `cancelFade(const std::string& fade_id, bool snap_to_end)`

**Brief**: Cancel an active fade, optionally sending a final OSC value.

**Preconditions**: Called from tick or drain thread context.

**Behaviour**:
1. Look up `fade_id` in `fades_`. If not found: log warning, return.
2. If `snap_to_end == true`: call `OscSender::sendFloat(osc_target, osc_path, end_value)`.
3. Compute OSC key; erase from `osc_index_`.
4. Erase from `fades_` (destructor calls `lo_address_free`, `~Curve`).

**Returns**: void.

**Throws**: Never.

---

## `cancelAll()`

**Brief**: Cancel every active fade without sending final OSC values.

**Preconditions**: Called from tick or drain thread context. Typically triggered by project unload / SIGTERM.

**Behaviour**:
1. Iterate `fades_`; for each: call `lo_address_free(osc_target)`.
2. Clear `fades_` and `osc_index_`.

**Returns**: void.

**Throws**: Never.

---

## `tick(long mtc_ms)`

**Brief**: Evaluate all active fades at `mtc_ms` and send OSC. Call after draining the command queue.

**Preconditions**:
- Called exclusively from the MTC tick thread.
- `addFade`, `cancelFade`, `cancelAll` have already been applied from the drained queue before this call (ensures no concurrent modification).

**Behaviour**: For each `ActiveFade` in `fades_`:
1. Compute `t = clamp((mtc_ms - start_mtc_ms) / duration_ms, 0.0, 1.0)`.
   - Special case: if `duration_ms == 0`, `t = 1.0`.
2. Compute `value = start_value + (end_value - start_value) * curve->evaluate(t)`.
3. `int ret = OscSender::sendFloat(osc_target, osc_path.c_str(), (float)value)`.
4. `last_sent_value = (float)value`.
5. If `ret != 0`: increment `consecutive_osc_failures`. If `>= kOscFailureThreshold`: push `FadeError:"osc_send_failed"`, mark for removal. Else: reset `consecutive_osc_failures = 0`.
6. If `t >= 1.0`: mark `completed = true`, push `FadeComplete`.

After iterating: remove all fades marked for removal or completed from both `fades_` and `osc_index_`.

**Returns**: void.

**Throws**: Never. `OscSender::sendFloat` is `noexcept`.

---

## Constants

| Name | Value | Description |
|------|-------|-------------|
| `kOscFailureThreshold` | `5` | Consecutive `lo_send` failures before a fade is declared dead. |
| `kStatusQueueCapacity` | `64` | Matches inbound `LockFreeQueue` capacity. |
