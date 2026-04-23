# Quickstart: Adapt MtcTickSource to mtcreceiver v2.0.0

**Feature**: 004-adapt-mtc-tick-v2
**Audience**: Engineer picking up the implementation branch after
`/speckit.plan` finishes.

This is a compact, end-to-end walkthrough of the implementation and
validation steps. It is NOT the task list — `/speckit.tasks` will expand
these into granular, ordered tasks.

## 1. Bump the submodule pin

```bash
cd mtcreceiver
git fetch origin
git checkout 59fc76e
cd ..
git add mtcreceiver
git status   # should show: modified: mtcreceiver (new commits)
```

Verify:

```bash
git -C mtcreceiver rev-parse HEAD   # → 59fc76e... (FR-001)
```

## 2. Migrate the adapter (src/time/MtcTickSource.cpp)

Replace the one-liner:

```cpp
void MtcTickSource::setTickCallback(std::function<void(long)> cb) {
    MtcReceiver::onQuarterFrame = std::move(cb);      // OLD (v1)
}
```

with the v2.0.0 adapter:

```cpp
void MtcTickSource::setTickCallback(std::function<void(long)> cb) {
    if (cb) {
        MtcReceiver::setTickCallback(
            [cb = std::move(cb)](long ms, bool /*isCompleteFrame*/) {
                cb(ms);
            });
    } else {
        MtcReceiver::setTickCallback({});
    }
}

MtcTickSource::~MtcTickSource() {
    MtcReceiver::setTickCallback({});   // no-call-after-dtor (FR-004)
}
```

## 3. Update the header (src/time/MtcTickSource.h)

- Replace `~MtcTickSource() = default;` with `~MtcTickSource();`.
- Update the `setTickCallback` docstring to reflect the new thread-safe
  contract (no "must call before start()" restriction) and the ignored
  `isCompleteFrame` flag.
- Correct the rate comment: **100 Hz at 25 fps** (was incorrectly 200 Hz).

## 4. Enable the mtcreceiver testing API on the test target

In `tests/CMakeLists.txt`, add a compile definition to `test_mtc_tick_source`:

```cmake
target_compile_definitions(test_mtc_tick_source PRIVATE MTCRECV_TESTING)
```

Keep the library and daemon builds free of this flag.

## 5. Rewrite the unit tests (tests/test_mtc_tick_source.cpp)

- `resetMtcState()` now calls `MtcReceiver::setTickCallback({})` and
  `MtcReceiver::resetStaticStateForTesting()`. The direct assignment
  `MtcReceiver::onQuarterFrame = nullptr` is deleted.
- Construct the receiver via the `SkipPortOpenTag` overload so tests do
  not depend on MIDI hardware.
- Replace every `MtcReceiver::onQuarterFrame(val)` call with
  `MtcReceiver::invokeTickForTesting(val, isCompleteFrame)`.
- Apply the mixed 7× `false` + 1× `true` pattern (per 8-QF cycle) in
  Scenarios A, C, and the synthetic stream (FR-008).
- Repurpose Scenario B as the **single-fire-per-QF contract** test
  (FR-005): each `invokeTickForTesting` call produces exactly one
  consumer invocation regardless of the flag value.
- Delete the synthetic-stream comment block that reasons about 60 s vs
  12 s — the new ground truth is 1200 ticks × 10 ms = **12 s**
  (FR-006, SC-003).
- Scenario E: with `SkipPortOpenTag`, the port-matching tests against a
  real `start()` stay; they now have deterministic outcomes on hosts
  without MIDI because the testing constructor is used only where
  port-opening must be suppressed.

## 6. Build and run

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Expected:

- Zero compile errors / warnings related to mtcreceiver API usage
  (SC-001).
- `test_mtc_tick_source` passes all scenarios, total ctest runtime under
  30 s (SC-002).
- Synthetic stream delivers exactly 1200 callbacks, final MTC head
  within ±1 ms of 12000 ms (SC-003).

## 7. Sanity checks before PR

- Destructor test (SC-004): fire a synthetic tick *after* the
  `MtcTickSource` destructor runs (wrap lifetime in a block, then invoke
  `invokeTickForTesting` from outside). Consumer callback count must be
  zero post-destruction.
- Thread-safety (SC-005): run the binary under `-fsanitize=thread` (or a
  dedicated TSan build) and confirm zero race reports in
  `MtcTickSource` / `MtcReceiver` code paths.
- Latency (SC-006): already exercised by the synthetic-stream test's
  callback-count timing. No dedicated microbenchmark added — the adapter
  is a single std::function call over the v1 baseline.

## 8. Commit discipline

One commit per constitution-compliant, independently-compilable unit.
Suggested minimum split:

1. `chore: bump mtcreceiver to v2.0.0 (59fc76e)` — submodule pointer only.
2. `feat(time): adapt MtcTickSource to mtcreceiver v2.0.0 setTickCallback`
   — adapter + destructor + header docstrings.
3. `test(time): rewrite MtcTickSource tests against v2.0.0 testing helpers`
   — test file + CMakeLists `MTCRECV_TESTING` flag.

Commits (1) alone must compile the submodule with no gradient-motion
changes (it will fail to compile the current adapter — this is expected
and confines the breakage window to a single transient commit).
Therefore land (1) + (2) together on main, or squash-merge the branch.
