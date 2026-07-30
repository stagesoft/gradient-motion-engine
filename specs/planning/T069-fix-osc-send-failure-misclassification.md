<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# T069 — Fix OSC Send-Failure Misclassification in `FadeMotion::evalAndSend`

**Issued by**: cuems-engine debugging session (`fix/gradient-engine-action` branch)
**Target repo**: `gradient-motion-engine`
**Primary file**: `src/motion/FadeMotion.cpp`
**Context version**: v0.3.0 (`debian/changelog` top entry `cuems-gradient-motiond (0.3.0-1)`)
**Date**: 2026-07-29
**Status**: plan — not yet applied. To be validated per the checklist below, then folded into the package (changelog entry + version bump) once green.

---

## Background

While debugging a separate, already-fixed `node_name`-filter bug in cuems-engine
(`GradientClient` was sending the node's UUID instead of the daemon's
`--node-name`), we confirmed the daemon now correctly *receives*
`/gradient/start_fade`. But the downstream OSC tick to the target player
(`osc_host:osc_port:osc_path`) never arrives, or arrives for only a handful of
ticks before the fade silently dies.

### Root cause (verified, not hypothesized)

`FadeMotion::evalAndSend` (`src/motion/FadeMotion.cpp:81-89`):

```cpp
const int ret = oscSend_(osc_target_, osc_path.c_str(), value);  // -> lo_send()
...
r.failed = (ret != 0);
```

`oscSend_` defaults to `gme::osc::sendFloat` (`src/osc/OscSender.cpp:20-24`),
which forwards directly to liblo's `lo_send()`. Per `/usr/include/lo/lo.h:128`,
`lo_send` returns **`-1` on failure**; on success it returns the number of
bytes sent — **never `0`**. This was verified empirically, not just read from
the header comment:

```c
// compiled against the installed liblo.so.7 (0.32-2)
lo_address addr = lo_address_new("127.0.0.1", "9999");
int ret = lo_send(addr, "/test/path", "f", 0.5f);
printf("lo_send returned: %d\n", ret);   // -> 20
```

So `ret != 0` is **true on every successful send**, not just on failures.
`MotionRegistry::tick` (`src/motion/MotionRegistry.cpp:173-186`) increments
`m.consecutive_osc_failures` whenever `r.failed`, and at
`kOscFailureThreshold = 5` (`src/motion/MotionRegistry.h:74-81`, "at 200 Hz
this equals 25 ms") emits `MotionError:"osc_send_failed"` and removes the
motion. Net effect: **every fade with `duration_ms` greater than ~25 ms is
killed almost immediately after starting**, having sent only a handful of
ticks near `start_value`. The daemon logs this at INFO level — confirmed live
against `cuems-gradient-motiond` on the test box via
`journalctl -u cuems-gradient-motiond`, which showed
`GradientEngine: MotionError motion_id=... reason=osc_send_failed` within
~25 ms of the corresponding `start_fade`.

### Why the existing test suite didn't catch it

Every `OscSendFn` mock in `tests/test_fade_motion.cpp` and
`tests/test_motion_registry.cpp` uses the **opposite, POSIX-style**
convention — `return 0;` for success, `return -1;` for injected failure
(e.g. `tests/test_fade_motion.cpp:150,167,189,213,228,321,361,380,398,496,510,524`
and the explicit failure-injection tests at `:443-446,470-473`). No existing
test ever passes a positive "success" value through the send function, so
nothing exercises the real liblo return convention. `MotionRegistry.h:93` /
`FadeMotion.h:65` declare `OscSendFn = std::function<int(lo_address, const
char*, float)>` with no documented return-value contract — that omission is
the actual gap that let the mismatch through code review.

### Fix

`FadeMotion.cpp:88`: `r.failed = (ret != 0);` → `r.failed = (ret < 0);`

---

## Execution checklist

### 1. Checkout

```bash
cd /disk/Projects/StageLab/gradient-motion-engine
git status                          # confirm clean tree before starting
git checkout -b fix/osc-send-failure-threshold-t069
```

### 2. Regression test FIRST (TDD — confirm it fails before the fix)

Add to `tests/test_fade_motion.cpp`, in the US4 section (after
`test_us4_transient_failure_recovery`, ~line 487), following the existing
`TestCtx`/`ASSERT_TRUE` idiom:

```cpp
static bool test_us4_real_lo_send_success_not_misclassified() {
    // Regression for T069: liblo's lo_send() returns bytes-sent (a positive,
    // non-zero int) on success, never 0. A mock that mirrors this — instead
    // of the POSIX-style "0 = success" convention every other test in this
    // file uses — must NOT trip the failure counter.
    TestCtx ctx;
    auto send = [](lo_address, const char*, float) -> int { return 20; };
    auto reg = ctx.makeRegWithSend(send);

    auto cmd = TestCtx::makeCmd("freal", "linear", 0.0f, 1.0f, 10000.0f, 0);
    reg->apply(cmd);

    for (int i = 0; i < gme::motion::MotionRegistry::kOscFailureThreshold; ++i) {
        reg->tick(i * 10);
    }

    ASSERT_TRUE(reg->size() == 1,
                "us4_real_lo_send: fade must survive N ticks of a real "
                "liblo-style positive return value");

    for (auto& r : ctx.emitted)
        ASSERT_TRUE(r.reason != "osc_send_failed",
                    "us4_real_lo_send: no spurious osc_send_failed");
    return true;
}
```

Register it in `main()`'s `tests[]` array (~line 468):
```cpp
{"us4_real_lo_send_not_misclassified", test_us4_real_lo_send_success_not_misclassified},
```

Build and run — **this test must FAIL on the current code** (proves the
regression test actually exercises the bug before touching production code):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -R test_fade_motion
```
Expected: `FAIL [us4_real_lo_send: ...]` and the ctest case for
`test_fade_motion` reports failure. If it passes before the fix, stop —
something about the reproduction is wrong and the fix must not proceed on a
false premise.

### 3. Apply the fix

`src/motion/FadeMotion.cpp:88`:
```diff
-    r.failed         = (ret != 0);
+    r.failed         = (ret < 0);
```

Re-run the same build/test command. All of `test_fade_motion` (including the
new case and the two pre-existing US4 tests, which still use the POSIX-style
mock and must keep passing since `-1 < 0` is still `true`) must pass:

```bash
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Run the **full** suite, not just `test_fade_motion` — confirm no other test
implicitly depended on the old (buggy) comparison:
```bash
ctest --test-dir build --output-on-failure
```

### 4. Document the return-value contract (prevents recurrence)

Add a one-line contract note to the `OscSendFn` declarations so the next
person wiring a mock doesn't repeat the mismatch:

- `src/motion/FadeMotion.h:65` (near the `OscSendFn` typedef)
- `src/motion/MotionRegistry.h:93` (same)

```cpp
// Return convention matches liblo's lo_send(): >= 0 (bytes sent) on
// success, < 0 on failure. NEVER assume 0 == success — lo_send never
// returns exactly 0 for a non-empty message.
using OscSendFn = std::function<int(lo_address, const char*, float)>;
```

### 5. Additional logging

The current failure path logs only the terminal `MotionError:"osc_send_failed"`
at INFO once the threshold is hit — there is no visibility into individual
tick-level send failures leading up to it, which made this bug hard to
distinguish from "daemon never received the command at all" during the
original investigation. Add:

- **`FadeMotion::evalAndSend`** (`FadeMotion.cpp`, right after the `oscSend_`
  call): on `ret < 0`, emit a `GME_LOG_WARNING` (via a callback or a direct
  include of `logging.h` — check whether `FadeMotion` currently has any
  logging dependency; if not, prefer threading a lightweight log callback
  through `MotionRegistry::tick` rather than adding a hard dependency from
  `src/motion/` on `daemon/`'s logging, to keep the library/daemon split
  intact per `GradientEngine.cpp`'s comment about `libgradient_motion` vs the
  daemon binary) including `motion_id`, `osc_path`, and the raw `ret` value:
  ```
  WARNING FadeMotion: osc send failed motion_id=<id> path=<osc_path> lo_send_ret=<ret>
  ```
  Rate-limit or dedupe if this proves noisy in practice (a real dead target
  will log this every tick for `kOscFailureThreshold` ticks before removal —
  5 lines is fine, no rate-limiting needed at that volume).
- **`MotionRegistry::tick`** (`MotionRegistry.cpp:173-186`): when a motion
  recovers from a nonzero `consecutive_osc_failures` count back to 0 (i.e.
  transient failure that self-healed), log at DEBUG:
  `DEBUG MotionRegistry: motion_id=<id> osc send recovered after <N> failures`.
  This distinguishes "always worked" from "flaked and recovered" in the
  journal without adding INFO-level noise to the common case.

### 6. Version bump

Source of truth: `CMakeLists.txt:28` → `project(gradient-motion-engine VERSION 0.3.0 ...)`.
This is a bug fix, not a feature — bump the patch component per the existing
convention (`git log` shows prior bumps as a single commit touching
`CMakeLists.txt` + `debian/changelog` + `CHANGELOG.md` together):

- `CMakeLists.txt:28`: `VERSION 0.3.0` → `VERSION 0.3.1`
- `debian/changelog`: new top entry:
  ```
  cuems-gradient-motiond (0.3.1-1) unstable; urgency=medium

    * Fix FadeMotion::evalAndSend misclassifying every successful lo_send()
      as a failure (ret != 0 instead of ret < 0), which silently killed
      every fade with duration_ms > ~25ms via the 5-consecutive-failure
      threshold. Add OscSendFn return-value contract docs and warning-level
      logging on individual send failures.

   -- <packager name> <email>  <RFC 2822 date>
  ```
- `CHANGELOG.md`: new `## [0.3.1] - 2026-07-29` entry under a `### Fixed`
  heading, referencing this plan file (`specs/planning/T069-...md`).

### 7. Build the package

Per `debian/rules` (standard `dh $@`, configured via
`override_dh_auto_configure` with `-DBUILD_TESTS=OFF` for the release build):

```bash
cd /disk/Projects/StageLab/gradient-motion-engine
dpkg-buildpackage -us -uc -b
```

This produces `../cuems-gradient-motiond_0.3.1-1_amd64.deb` (and the
`-dbgsym` companion) in the parent directory, matching the naming pattern of
the currently-installed `cuems-gradient-motiond_0.3.0-1_amd64.deb`.

### 8. Pre-deploy validation (before this file is "passed into the package")

- [ ] `ctest --test-dir build --output-on-failure` — full suite green,
      including the new regression test.
- [ ] New regression test fails on a clean revert of the one-line fix
      (sanity-check the test isn't vacuously passing).
- [ ] `dpkg-buildpackage` completes without error; `dpkg -c` on the resulting
      `.deb` shows the updated `/usr/bin/gradient-motiond`.
- [ ] Manual smoke test on the dev/test box (`10.16.10.3`, per prior session):
      install the new `.deb`, restart `cuems-gradient-motiond`, replay the
      same raw `/gradient/start_fade` probe used earlier in this
      investigation (correct `node_name`, `duration_ms` well above 25ms —
      e.g. 5000), and confirm via `journalctl -u cuems-gradient-motiond
      --since <probe time>` that `MotionComplete` appears **after** roughly
      `duration_ms` has elapsed (not ~25ms later), with **no**
      `MotionError:"osc_send_failed"` in between.
  - [ ] Independently confirm the downstream player actually received the
      ticks (e.g. a throwaway UDP listener on the probe's `osc_port`, or
      observing the real player's parameter change) — the journal check
      alone only proves the daemon *thinks* it succeeded now, which is
      exactly the class of assumption that caused this bug in the first
      place; corroborate with an actual received packet.

### 9. Rollback plan

If the fix regresses something unexpected: `dpkg -i` the previously-built
`cuems-gradient-motiond_0.3.0-1_amd64.deb` (already present in
`/disk/Projects/StageLab/`) and restart the service. No schema/data
migration is involved — this is a pure logic fix with no persistent state,
so rollback is a straight binary swap.

---

## Related artifacts

| Artifact | Location |
|---|---|
| Daemon source of the bug | `src/motion/FadeMotion.cpp:81-89` |
| Threshold logic | `src/motion/MotionRegistry.cpp:173-186`, `MotionRegistry.h:74-81` |
| liblo contract | `/usr/include/lo/lo.h:122-130` |
| Existing test harness pattern | `tests/test_fade_motion.cpp` (`TestCtx`, `test_us4_osc_failure_threshold`) |
| cuems-engine node_name fix (separate, already applied) | cuems-engine `src/cuemsengine/players/GradientClient.py`, `NodeEngine.py::_resolve_gradient_node_name` |
