# Tasks: Gradient Curves Module (gme::gradient)

**Input**: Design documents from `/specs/002-gradient-curves/`
**Prerequisites**: plan.md ✓, spec.md ✓, research.md ✓, data-model.md ✓, contracts/ ✓, quickstart.md ✓

**Tests**: Included — the feature specification explicitly calls for `tests/test_curves.cpp` covering all curve types, decorators, and the factory.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

**Note on US ordering**: The spec lists US2 (Factory) before US3 (ResampledCurve), but the factory depends on ResampledCurve (it wraps curves by default). Implementation phases follow the dependency order: US1 → US3 → US2 → US4 → US5.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)

---

## Phase 1: Setup (Build Infrastructure)

**Purpose**: Prepare the CMake build system to accept the new source files. The Phase 0 placeholder must be replaced before any new `.cpp` file can be compiled.

- [x] T001 Remove `gradient/gradient.cpp` from the source list in `src/CMakeLists.txt` and add the four new implementation sources: `gradient/SigmoidCurve.cpp`, `gradient/BezierCurve.cpp`, `gradient/ResampledCurve.cpp`, `gradient/CurveFactory.cpp`
- [x] T002 Add `nlohmann_json::nlohmann_json` to the `target_link_libraries` call for the `gradient_motion` target in `src/CMakeLists.txt` (or the top-level `CMakeLists.txt` if that is where `target_link_libraries` for `gradient_motion` lives — check first)
- [x] T003 Add the test binary target to `tests/CMakeLists.txt` (or create `tests/CMakeLists.txt` if absent): `add_executable(test_curves tests/test_curves.cpp)`, link against `gradient_motion`, register with `add_test(NAME test_curves COMMAND test_curves)`

---

## Phase 2: Foundational (Abstract Interface)

**Purpose**: `src/gradient/Curve.h` is the single coupling point for the entire module. Every concrete class, decorator, and factory depends on this header. Nothing else can be written until this contract is locked.

**⚠️ CRITICAL**: No user story implementation can begin until this phase is complete.

- [x] T004 Create `src/gradient/Curve.h` — define `namespace gme::gradient`, abstract class `Curve` with `virtual double evaluate(double t) const = 0` and `virtual ~Curve() = default`. Declare copy constructor and copy assignment as deleted (`= delete`); declare move constructor and move assignment as defaulted (`= default`) — required so that `std::unique_ptr<Curve>` ownership in decorators (ScaledCurve, CrossfadePair) compiles correctly. Add full Doxygen docstring: brief, `\param t`, `\return`, clamping precondition, postconditions for boundary values, thread-safety note, and a `\code`/`\endcode` example block showing typical instantiation and `evaluate()` call using a concrete subclass.

**Checkpoint**: Foundation ready — all user story phases can now begin.

---

## Phase 3: User Story 1 — Evaluate a Fade Curve at Any Point in Time (Priority: P1) 🎯 MVP

**Goal**: Deliver all concrete curve implementations (linear, sigmoid, bezier, ease-in, ease-out, s-curve) so that any curve can be instantiated, called with a normalized progress value, and return a correct normalized output.

**Independent Test**: Build `test_curves` binary and run it. The US1 test block verifies: boundary values (t=0 → 0.0, t=1 → 1.0) for all six curve types, monotonicity for non-bezier types, and input clamping for t < 0 and t > 1.

### Implementation for User Story 1

- [x] T005 [P] [US1] Create `src/gradient/LinearCurve.h` — header-only class in `gme::gradient` inheriting `Curve`. `evaluate(t)`: clamp t to [0,1] then `return t`. Doxygen docstring including a `\code`/`\endcode` example showing instantiation and a sample `evaluate(0.5)` call.
- [x] T006 [P] [US1] Create `src/gradient/EaseInCurve.h` — header-only class in `gme::gradient`. Constructor takes `double exponent = 2.0`. `evaluate(t)`: clamp then `return std::pow(t, exponent)`. Doxygen docstring including default value for `exponent` and a `\code`/`\endcode` example.
- [x] T007 [P] [US1] Create `src/gradient/EaseOutCurve.h` — header-only class in `gme::gradient`. Constructor takes `double exponent = 2.0`. `evaluate(t)`: clamp then `return 1.0 - std::pow(1.0 - t, exponent)`. Doxygen docstring including default value for `exponent` and a `\code`/`\endcode` example.
- [x] T008 [P] [US1] Create `src/gradient/SCurve.h` — header-only class in `gme::gradient`. Stateless. `evaluate(t)`: clamp then `return 3.0*t*t - 2.0*t*t*t`. Doxygen docstring including the smoothstep formula in `\details` and a `\code`/`\endcode` example.
- [x] T009 [P] [US1] Create `src/gradient/SigmoidCurve.h` — declare class in `gme::gradient`. Constructor params: `double steepness = 8.0`, `double midpoint = 0.5`. Private members: `steepness_`, `midpoint_`, `norm_offset_` (= raw(0)), `norm_scale_` (= 1/(raw(1)-raw(0))). Private `raw(double t)` helper: `1.0 / (1.0 + std::exp(-steepness_ * (t - midpoint_)))`. `evaluate(t)` declared. Full Doxygen including default parameter values, normalization strategy in `\details`, and a `\code`/`\endcode` example showing construction with custom steepness and midpoint.
- [x] T010 [US1] Create `src/gradient/SigmoidCurve.cpp` — implement constructor (compute and cache `norm_offset_` and `norm_scale_`). Implement `evaluate(t)`: clamp t, then `return (raw(t) - norm_offset_) * norm_scale_`.
- [x] T011 [P] [US1] Create `src/gradient/BezierCurve.h` — declare class in `gme::gradient`. Constructor params: `double cx1 = 0.25, double cy1 = 0.1, double cx2 = 0.75, double cy2 = 0.9`. Private members storing the four control point coordinates. Fixed endpoints P0=(0,0), P3=(1,1). `evaluate(t)` declared. Full Doxygen including default parameter values, De Casteljau method note in `\details`, non-monotonicity caveat (control points outside [0,1] are permitted), and a `\code`/`\endcode` example.
- [x] T012 [US1] Create `src/gradient/BezierCurve.cpp` — implement `evaluate(t)`: clamp t, then apply De Casteljau's algorithm for cubic bezier using P0=(0,0), P1=(cx1_,cy1_), P2=(cx2_,cy2_), P3=(1,1) with parameter t. Return the y-coordinate of the final interpolated point.

### Tests for User Story 1

- [x] T013 [US1] Create `tests/test_curves.cpp` with a `main()` that calls all test functions and returns 0 on success / 1 on first failure. Implement the following test functions for US1:
  - `test_boundary_values()` — instantiate LinearCurve, SigmoidCurve (defaults), BezierCurve (defaults), EaseInCurve (defaults), EaseOutCurve (defaults), SCurve; for each assert evaluate(0.0)==0.0 and evaluate(1.0)==1.0
  - `test_monotonicity()` — for LinearCurve, SigmoidCurve, EaseInCurve, EaseOutCurve, SCurve: sample at 100 evenly-spaced t values and assert each output >= previous
  - `test_input_clamping()` — for LinearCurve, SigmoidCurve, and BezierCurve (default params each): assert evaluate(-0.5)==0.0 and evaluate(1.5)==1.0; ensures clamping is not accidentally omitted from non-trivial curve types
  - `test_sigmoid_extreme_steepness()` — construct SigmoidCurve(50.0, 0.5) (extreme steepness stresses normalization arithmetic); assert evaluate(0.0)==0.0 and evaluate(1.0)==1.0 exactly (normalization must hold regardless of steepness)

**Checkpoint**: Run `ctest`. Six curve types pass boundary, monotonicity, and clamping checks.

---

## Phase 4: User Story 3 — Performant Curve Evaluation During Playback (Priority: P1)

**Goal**: Deliver `ResampledCurve` — a decorator that pre-computes any `Curve` into a 256-sample LUT at construction, then performs constant-time linear interpolation at evaluate() with no transcendental math.

**Note**: This phase is implemented before US2 (Factory) because the factory wraps all returned curves in `ResampledCurve` by default. `ResampledCurve` must exist first.

**Independent Test**: Extend `tests/test_curves.cpp`. Verify resampled output matches original SigmoidCurve output to within 0.5% at 1000 evenly-spaced points.

### Implementation for User Story 3

- [x] T014 [US3] Create `src/gradient/ResampledCurve.h` — declare class in `gme::gradient` inheriting `Curve`. Constructor: `ResampledCurve(const Curve& source, int samples = 256)`. `const Curve&` is safe here because `source` is only used during construction to fill the LUT; no reference is retained after the constructor returns. Private members: `int samples_`, `std::vector<double> lut_`. `evaluate(t)` declared. Full Doxygen including: LUT lifecycle note (source used only at construction, safe to destroy afterwards), memory note (samples × 8 bytes), default sample count, and a `\code`/`\endcode` example wrapping a SigmoidCurve.
- [x] T015 [US3] Create `src/gradient/ResampledCurve.cpp` — implement constructor: allocate `lut_` with `samples_` entries, for i in [0, samples_-1] set `lut_[i] = source.evaluate(i / double(samples_ - 1))`. Implement `evaluate(t)`: clamp t, compute fractional index `f = t * (samples_ - 1)`, floor to `idx`, linear-interpolate between `lut_[idx]` and `lut_[idx+1]` using fractional part (guard against idx == samples_-1).

### Tests for User Story 3

- [x] T016 [US3] Add test functions to `tests/test_curves.cpp`:
  - `test_resampled_accuracy()` — wrap SigmoidCurve(10.0, 0.5) in ResampledCurve(256); at 1000 uniformly-spaced t values, assert |resampled.evaluate(t) - direct.evaluate(t)| < 0.005 (0.5%)
  - `test_resampled_boundary()` — assert ResampledCurve wrapping any curve satisfies evaluate(0.0)==0.0 and evaluate(1.0)==1.0
  - `test_resampled_n2()` — construct `ResampledCurve(LinearCurve{}, 2)` (degenerate sample count from spec edge cases); assert no crash, evaluate(0.0)==0.0, evaluate(1.0)==1.0, and evaluate(0.5) is between 0.0 and 1.0

**Checkpoint**: Run `ctest`. ResampledCurve accuracy test passes; no transcendental calls during evaluate().

---

## Phase 5: User Story 2 — Select a Curve Shape by Name (Priority: P1)

**Goal**: Deliver `CurveFactory` — a static factory that maps a string curve type name plus `nlohmann::json` parameters to a `std::optional<std::unique_ptr<Curve>>`. Known types return a populated optional containing a `ResampledCurve`-wrapped curve; unknown types return `std::nullopt` so callers can detect and handle absence explicitly.

**Independent Test**: Extend `tests/test_curves.cpp`. Verify all 6 type names return a populated optional with a working curve; unknown name returns nullopt; missing params apply defaults.

### Implementation for User Story 2

- [x] T017 [US2] Create `src/gradient/CurveFactory.h` — declare `namespace gme::gradient`, class `CurveFactory` with static method `static std::optional<std::unique_ptr<Curve>> createCurve(const std::string& type, const nlohmann::json& params = {})`. Include `<memory>`, `<optional>`, `<string>`; include or forward-declare nlohmann/json. Write a thorough Doxygen docstring for `createCurve` covering: (a) **brief** — one-line summary; (b) **\param type** — list all supported names: "linear", "sigmoid", "bezier", "ease_in", "ease_out", "scurve"; state that any other string returns nullopt; (c) **\param params** — JSON object; document per-type keys with their types and defaults (e.g., `"steepness"` double default 8.0, `"midpoint"` double default 0.5 for sigmoid); state that missing keys silently use defaults; (d) **\return** — explain that on success the optional contains an owned `ResampledCurve`-wrapped instance; on unknown type it contains no value (`std::nullopt`); (e) **\note** — explain *why* `std::optional` rather than a raw pointer or silent fallback: returning `std::nullopt` makes the unknown-type case observable at the call site, allowing the caller (e.g., the engine's FadeRegistry) to apply its own policy (log, alert operator, substitute a default) rather than silently substituting a linear curve that may misrepresent the intended cue shape; (f) **error handling** — state that the function never throws; malformed params use defaults; (g) **\code/\endcode** example showing: (1) successful creation and use, and (2) the recommended fallback pattern using `value_or` or explicit `has_value()` check.
- [x] T018 [US2] Create `src/gradient/CurveFactory.cpp` — implement `createCurve`: use an if-else chain or `std::unordered_map` keyed on `type`. For each known type, extract params from JSON with `.value("key", default)`, construct the concrete curve, wrap in `ResampledCurve`, and return `std::make_optional(std::move(wrapped))`. On unknown type, write a single-line warning to `stderr` (`"[CurveFactory] Unknown curve type: '" + type + "' — returning nullopt"`) and `return std::nullopt`. Do not construct a LinearCurve fallback inside the factory.

### Tests for User Story 2

- [x] T019 [US2] Add test functions to `tests/test_curves.cpp`:
  - `test_factory_all_types()` — call `CurveFactory::createCurve` with each of: "linear", "sigmoid", "bezier", "ease_in", "ease_out", "scurve"; assert `result.has_value()` is true and `(*result)->evaluate(0.0)==0.0` and `(*result)->evaluate(1.0)==1.0`
  - `test_factory_unknown_returns_nullopt()` — call with "nonexistent_curve"; assert `!result.has_value()` (nullopt returned, no crash, no fallback curve)
  - `test_factory_default_params()` — call "sigmoid" with empty JSON `{}`; assert result has value and evaluates 0.0→0.0, 1.0→1.0 (defaults applied)
  - `test_factory_caller_fallback_pattern()` — call with "nonexistent_curve"; demonstrate the recommended fallback: `auto curve = result ? std::move(*result) : std::make_unique<ResampledCurve>(LinearCurve{});` assert curve is non-null and curve->evaluate(0.5)==0.5 (caller-applied linear fallback)

**Checkpoint**: Run `ctest`. All six type names return populated optionals with valid curves; unknown type returns nullopt; caller fallback pattern works correctly.

---

## Phase 6: User Story 4 — Remap Curve Output to Arbitrary Value Ranges (Priority: P2)

**Goal**: Deliver `ScaledCurve` — a header-only decorator that wraps any `Curve` and applies configurable input and output range remapping.

**Independent Test**: Extend `tests/test_curves.cpp`. Verify range mapping at boundaries and midpoint; verify composition with ResampledCurve works.

### Implementation for User Story 4

- [x] T020 [US4] Create `src/gradient/ScaledCurve.h` — header-only class in `gme::gradient` inheriting `Curve`. Constructor: `ScaledCurve(std::unique_ptr<Curve> inner, double in_min=0.0, double in_max=1.0, double out_min=0.0, double out_max=1.0)` — takes ownership of the inner curve via `unique_ptr` (NOT `const Curve&`; a reference member to a temporary would be a dangling reference after the constructor returns, as shown in the decorator composition example `ScaledCurve(ResampledCurve(SigmoidCurve(...)), ...)` where the intermediate is a temporary). Private member: `std::unique_ptr<Curve> inner_`. `evaluate(t)`: normalize input `norm_t = (t - in_min_) / (in_max_ - in_min_)`, clamp norm_t to [0,1], call `inner_->evaluate(norm_t)`, map output `return out_min_ + result * (out_max_ - out_min_)`. Full Doxygen including: mapping formula in `\details`, ownership transfer note, and a `\code`/`\endcode` example using `std::make_unique`.

### Tests for User Story 4

- [x] T021 [US4] Add test functions to `tests/test_curves.cpp`:
  - `test_scaled_output_range()` — wrap LinearCurve with out_min=0.0, out_max=0.5; assert evaluate(0.0)==0.0 and evaluate(1.0)==0.5 and evaluate(0.5)≈0.25
  - `test_scaled_input_range()` — wrap LinearCurve with in_min=0.0, in_max=2.0 (double input range); assert evaluate(1.0)≈0.5 (midpoint of input range)
  - `test_scaled_boundaries()` — wrap LinearCurve with in_min=0.25, in_max=0.75, out_min=0.1, out_max=0.9; assert evaluate(0.25)==0.1 and evaluate(0.75)==0.9

**Checkpoint**: Run `ctest`. ScaledCurve maps boundaries and midpoints correctly.

---

## Phase 7: User Story 5 — Generate Complementary Crossfade Values (Priority: P2)

**Goal**: Deliver `CrossfadePair` — a header-only wrapper that holds one `Curve` and returns both the primary value and its complement (1.0 - primary) to guarantee the pair always sums to 1.0.

**Independent Test**: Extend `tests/test_curves.cpp`. Verify primary + complement == 1.0 at all sampled points; verify boundary behaviour.

### Implementation for User Story 5

- [x] T022 [US5] Create `src/gradient/CrossfadePair.h` — header-only class in `gme::gradient` (does NOT inherit `Curve` — it returns a struct/pair, not a single double). Constructor: `CrossfadePair(std::unique_ptr<Curve> curve)` — takes ownership via `unique_ptr` (NOT `const Curve&`; storing a reference to a temporary is UB; the factory returns `unique_ptr<Curve>` so ownership transfer is the natural API). Private member: `std::unique_ptr<Curve> curve_`. Nested struct `Result { double primary; double complement; }` and `Result evaluate(double t)`. In `evaluate`: call `curve_->evaluate(t)` exactly once, store as `double v`, return `{v, 1.0 - v}` — single call guarantees `primary + complement == 1.0` arithmetically (two separate calls would allow floating-point divergence). Full Doxygen including: the sum invariant in `\invariant`, ownership transfer note, single-evaluate rationale, and a `\code`/`\endcode` example using `std::make_unique`.

### Tests for User Story 5

- [x] T023 [US5] Add test functions to `tests/test_curves.cpp`:
  - `test_crossfade_sum_invariant()` — create CrossfadePair wrapping SigmoidCurve; at 100 uniformly-spaced t values, assert primary + complement == 1.0 exactly
  - `test_crossfade_boundaries()` — assert evaluate(0.0).primary==0.0 and evaluate(0.0).complement==1.0; assert evaluate(1.0).primary==1.0 and evaluate(1.0).complement==0.0

**Checkpoint**: Run `ctest`. CrossfadePair sum-to-1.0 invariant verified at all sampled points.

---

## Phase 8: Polish & Cross-Cutting Concerns

**Purpose**: Final validation, build verification, and ctest confirmation for the complete module.

- [x] T024 [P] Verify the CMake build compiles cleanly from a fresh build directory: `cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . -- -j$(nproc)` — confirm zero warnings under `-Wall`
- [x] T025 Run `ctest --verbose` from the build directory and confirm `test_curves` passes all test functions; fix any assertion failures before marking complete
- [x] T026 [P] Delete `src/gradient/gradient.cpp` (the Phase 0 placeholder) from the repository — confirm it is removed from git tracking and the build still compiles cleanly after removal
- [x] T027 Add a micro-benchmark to `tests/test_curves.cpp` (or a standalone `tests/bench_curves.cpp` if preferred): call `ResampledCurve::evaluate()` in a loop of 1,000,000 iterations using `std::chrono::high_resolution_clock`; assert total time / iterations < 1000 ns (1 µs). This benchmark enforces Constitution IV's requirement that latency budgets be verified empirically, not just analytically. Register it with `add_test` and annotate it as a performance check (it may be skipped in CI debug builds via a `BENCHMARK` CTest label).
- [x] T028 [P] Run `cmake --build . --target docs` (enabled by the `doxygen_add_docs` target added to root CMakeLists.txt at project level). The build MUST complete with zero Doxygen warnings (enforced by `DOXYGEN_WARN_AS_ERROR YES`). Fix any missing `\param`, `\return`, or `\code` sections in `src/gradient/` headers until the docs target is clean. This satisfies Constitution VI's requirement for a documentation toolchain that builds without warnings. (SC-006 isolation note: this also confirms `src/gradient/` headers are self-contained — Doxygen parses only the gradient sources without daemon or protocol headers.)

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — can start immediately
- **Foundational (Phase 2)**: Depends on Setup — **BLOCKS all user stories**
- **US1 (Phase 3)**: Depends on Phase 2 (Curve.h)
- **US3 (Phase 4)**: Depends on Phase 3 (needs concrete Curve implementations to test accuracy against)
- **US2 (Phase 5)**: Depends on Phase 4 (ResampledCurve must exist before factory can wrap it)
- **US4 (Phase 6)**: Depends on Phase 2 (Curve.h) — can actually start after Phase 2, but logically belongs here
- **US5 (Phase 7)**: Depends on Phase 2 (Curve.h) — can actually start after Phase 2, but logically belongs here
- **Polish (Phase 8)**: Depends on all implementation phases complete

### User Story Dependencies

- **US1 (P1)** — no user story dependencies; depends only on `Curve.h`
- **US3 (P1)** — depends on US1 being available for accuracy comparison in tests
- **US2 (P1)** — depends on US3 (factory wraps in ResampledCurve by default)
- **US4 (P2)** — no user story dependencies; depends only on `Curve.h`
- **US5 (P2)** — no user story dependencies; depends only on `Curve.h`

### Within Each User Story

- Headers (`.h`) before implementations (`.cpp`)
- Header-only classes: single task
- Non-trivial implementations: `.h` task first, `.cpp` task second
- Tests written for each story after implementation (the spec does not require TDD; tests validate the story)

### Parallel Opportunities

- T005–T009, T011 (US1 headers) are all [P] — different files, no inter-file dependencies
- T005–T008 (trivial headers) can all run simultaneously once T004 (Curve.h) is complete
- T020 (ScaledCurve) and T022 (CrossfadePair) can run in parallel with each other and with US3/US2 work, since they only depend on Curve.h
- T024, T026, T027, and T028 in Phase 8 can all run in parallel (different concerns: build verification, placeholder removal, benchmark, docs)

---

## Parallel Example: User Story 1

```
# Once T004 (Curve.h) is done, launch all header-only curves simultaneously:
Task A: T005 — Create src/gradient/LinearCurve.h
Task B: T006 — Create src/gradient/EaseInCurve.h
Task C: T007 — Create src/gradient/EaseOutCurve.h
Task D: T008 — Create src/gradient/SCurve.h
Task E: T009 — Create src/gradient/SigmoidCurve.h
Task F: T011 — Create src/gradient/BezierCurve.h

# Then implement the .cpp files (depend on their respective .h):
Task G: T010 — Create src/gradient/SigmoidCurve.cpp  (after T009)
Task H: T012 — Create src/gradient/BezierCurve.cpp   (after T011)

# Then write tests (depend on all 6 curve types being complete):
Task I: T013 — Create tests/test_curves.cpp with US1 tests
```

---

## Implementation Strategy

### MVP First (US1 Only — 6 Concrete Curves)

1. Complete Phase 1: Setup (build system)
2. Complete Phase 2: Foundational (Curve.h interface)
3. Complete Phase 3: US1 (6 concrete curve types + US1 tests)
4. **STOP and VALIDATE**: Run `ctest` — confirm boundary, monotonicity, and clamping tests pass
5. Working curve evaluation available for manual integration testing

### Incremental Delivery

1. Setup + Foundational → build compiles with no sources yet
2. US1 → 6 curve types evaluating correctly → test_curves passes US1 block
3. US3 → ResampledCurve wrapping any curve → test_curves passes US3 block
4. US2 → Factory creating all curve types by name → test_curves passes US2 block
5. US4 → ScaledCurve range remapping → test_curves passes US4 block
6. US5 → CrossfadePair sum invariant → test_curves passes US5 block (complete module)

### Solo Developer Strategy

Work phases sequentially (P1 stories first):
1. Phase 1 → Phase 2 → Phase 3 → Phase 4 → Phase 5 → Phase 6 → Phase 7 → Phase 8
2. Validate with `ctest` at each story checkpoint before proceeding

---

## Notes

- [P] tasks = different files, no incomplete-task dependencies
- [Story] label maps task to specific user story for traceability
- Each user story is independently testable at its checkpoint
- The single test binary `tests/test_curves.cpp` grows incrementally — each phase adds test functions
- `src/gradient/gradient.cpp` (Phase 0 placeholder) is removed in Phase 8 after all new files are in place
- All headers require Doxygen docstrings per Constitution Principle VI (Documentation Standards)
- All concrete `evaluate()` implementations must clamp `t` to [0,1] at the start (per research.md R5)
