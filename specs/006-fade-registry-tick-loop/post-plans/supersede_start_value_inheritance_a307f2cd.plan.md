---
name: Supersede start_value inheritance
overview: In `FadeRegistry::addFade`, when a supersede is detected, capture the outgoing fade's `last_sent_value` before it is destroyed and use it as the new fade's `start_value` (and initial `last_sent_value`). This ensures a seamless OSC transition instead of a jump to the incoming command's `start_value`.
todos:
  - id: impl
    content: "In FadeRegistry.cpp addFade Step 3: capture last_sent_value before erasing the superseded fade; propagate to Step 5 as effective_start"
    status: completed
  - id: test
    content: Extend test_us2_supersede in test_fade_registry.cpp to assert fadeB's first OSC value equals the superseded fade's last_sent_value
    status: completed
isProject: false
---

# Supersede `start_value` Inheritance

## What changes

Single logical change, two locations in [`src/engine/FadeRegistry.cpp`](src/engine/FadeRegistry.cpp):

**Step 3 — capture `last_sent_value` before removing the superseded fade**

Current code in the `if (oit != osc_index_.end())` block:
```cpp
auto fit = fades_.find(superseded_id);
if (fit != fades_.end()) {
    if (fit->second->osc_target) {
        lo_address_free(fit->second->osc_target);
        fit->second->osc_target = nullptr;
    }
    fades_.erase(fit);
}
```

Add one line before `lo_address_free`:
```cpp
float inherited_start = fit->second->last_sent_value;
```

And declare a `float effective_start = cmd.start_value;` before the supersede block, then set `effective_start = inherited_start` inside the block.

**Step 5 — use `effective_start` instead of `cmd.start_value`**

```cpp
fade->start_value         = effective_start;   // was cmd.start_value
fade->last_sent_value     = effective_start;   // was cmd.start_value
```

`fade->end_value` stays `cmd.end_value` — the new fade's destination is unchanged.

## Test update

Add one assertion to `test_us2_supersede()` in [`tests/test_fade_registry.cpp`](tests/test_fade_registry.cpp):

After registering fadeB, tick once and verify the **first value sent by fadeB** equals the `last_sent_value` of fadeA at the moment of supersede (i.e. the linear value at `t = 1000/5000 = 0.2`, which is `0.2`), not fadeB's raw `start_value` of `0.5`.

## No header changes needed

`ActiveFade.h` and `FadeRegistry.h` are untouched — this is a pure behavioural change inside the `.cpp`.
