<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# T070 — Layered CI/CD Suite (GitHub Actions): TEST → COVERAGE → DOCUMENT → BUILD → DEPLOY

**Issued by**: packaging session following [T069](T069-fix-osc-send-failure-misclassification.md)
**Target repo**: `gradient-motion-engine`
**Primary files**: `.github/workflows/*.yml`, `tests/CMakeLists.txt`, `README.md`
**Context version**: v0.3.1 (`debian/changelog` top entry `cuems-gradient-motiond (0.3.1-1)`)
**Date**: 2026-07-29
**Status**: plan — not yet applied.
**Prerequisite (already landed)**: commit `9f1e0be` *"build(deb): make packaging build runner-safe; add real BUILD_TESTS option"* — the BUILD stage below depends on it.

---

## Background

The repo currently has two disconnected workflows:

| File | Does | Gap |
|---|---|---|
| `.github/workflows/tests.yml` | Build (Debug, `--coverage`) → modprobe ALSA → `ctest` (all 9 tests) → lcov → Codecov | Conflates TEST and COVERAGE; gates on the latency benchmarks, which are unfit for shared runners |
| `.github/workflows/docs.yml` | mkdocs + Doxygen → GitHub Pages | Runs independently of test outcome; can publish docs for a tree whose tests failed |

There is **no BUILD stage and no DEPLOY stage** — `.deb` packages are today built by hand on a workstation (that is how `cuems-gradient-motiond_0.3.1-1_amd64.deb` was produced) and moved to the fleet by hand. Nothing verifies that the version in `CMakeLists.txt`, `debian/changelog`, and `CHANGELOG.md` agree, which is exactly the kind of three-file bump T069 §6 performed manually.

The goal is a single layered pipeline where each stage gates the next.

---

## Constraints discovered in this repo (do not re-derive these)

1. **Two tests are latency-budgeted and will flake on shared runners.**
   `bench_osc_latency` (label `bench`) and `test_motion_registry_bench` (label
   `integration`) assert p99 budgets that a noisy-neighbour GitHub runner
   misses. They must be collected, not gated on.
2. **`test_motion_registry_bench` is mislabelled.** It carries
   `LABELS "integration"` (`tests/CMakeLists.txt:179`) despite being a
   benchmark, so `ctest -LE bench` does *not* exclude it. Relabel it to
   `bench`; that makes label-based gating actually work and leaves
   `integration` meaning only `test_osc_server_integration`, which is a
   legitimate gate.
3. **`bench_osc_latency` writes into the source tree.**
   `BENCH_RESULTS_FILE="${CMAKE_SOURCE_DIR}/tests/bench_results/osc_latency.txt"`
   (`tests/CMakeLists.txt:154`) — running it leaves the checkout dirty. That
   file is dirty in the working tree right now for exactly this reason.
4. **Submodules are mandatory.** `mtcreceiver` and `cuemslogger` are needed by
   the daemon; CMake hard-fails without them (`CMakeLists.txt:70-84`). Every
   job needs `submodules: recursive`.
5. **`DOXYGEN_WARN_AS_ERROR YES`** (`CMakeLists.txt:114`) — the docs target is
   already a strict gate; use it as one.
6. **Badge URLs are pinned to workflow filenames.** `README.md:15` points at
   `actions/workflows/tests.yml/badge.svg` and `:17` at `docs.yml`. Renaming
   or deleting either file silently breaks the badge. See §Decisions.
7. **The fleet is Debian, the default runner is Ubuntu.** `${shlibs:Depends}`
   resolves against the *build* distro, so an `ubuntu-latest` build stamps
   Ubuntu-flavoured dependencies (`liblo7`, `librtmidi6` version ranges) into
   a package destined for Debian nodes. The BUILD stage must run in a
   `debian:bookworm` container.
8. **`DEB_BUILD_MAINT_OPTIONS = hardening=+all`** is already set in
   `debian/rules`; no runner-side hardening flags are needed.

---

## Target pipeline

```
                    ┌────────────────────────────────────┐
   push / PR ──────▶│ 1. TEST      (gating, fast)        │
                    └───────────────┬────────────────────┘
                                    │ needs
             ┌──────────────────────┼──────────────────────┐
             ▼                      ▼                      ▼
   ┌──────────────────┐  ┌───────────────────┐  ┌────────────────────┐
   │ 2. COVERAGE      │  │ 3. DOCUMENT       │  │ 2b. BENCH          │
   │ lcov → Codecov   │  │ Doxygen + mkdocs  │  │ non-gating, upload │
   │ + coverage badge │  │ + badges          │  │ results as artifact│
   └────────┬─────────┘  └─────────┬─────────┘  └────────────────────┘
            └──────────┬───────────┘
                       ▼
            ┌──────────────────────────────┐
            │ 4. BUILD  (debian:bookworm)  │
            │ dpkg-buildpackage → .deb     │
            │ + version-consistency guard  │
            └──────────────┬───────────────┘
                           ▼  (tag v* / release published only)
            ┌──────────────────────────────┐
            │ 5. DEPLOY                    │
            │ attach .deb to GH Release    │
            └──────────────────────────────┘
```

Stages 2, 2b and 3 run in parallel once TEST is green. DEPLOY is the only
stage restricted by trigger.

---

## Execution checklist

### 0. Branch

```bash
git checkout main && git pull
git checkout -b feat/cicd-pipeline-t070
```

### 1. Make label-based gating meaningful (prerequisite edit)

`tests/CMakeLists.txt:179`:

```diff
-    set_tests_properties(test_motion_registry_bench PROPERTIES LABELS "integration")
+    # Latency-budgeted: grouped with the other benchmark so CI can gate on
+    # `ctest -LE bench` and collect these separately (see T070).
+    set_tests_properties(test_motion_registry_bench PROPERTIES LABELS "bench")
```

Verify the split is what the pipeline expects:

```bash
cmake -B build && ctest --test-dir build -N -LE bench   # expect 7 tests
cmake -B build && ctest --test-dir build -N -L  bench   # expect 2 tests
```

### 2. Make the benchmark output path overridable (constraint 3)

So the bench job does not dirty the checkout. `tests/CMakeLists.txt:153-155`:

```diff
+set(BENCH_RESULTS_DIR "${CMAKE_SOURCE_DIR}/tests/bench_results"
+    CACHE PATH "Directory bench_osc_latency writes its results to")
 target_compile_definitions(bench_osc_latency PRIVATE
-    BENCH_RESULTS_FILE="${CMAKE_SOURCE_DIR}/tests/bench_results/osc_latency.txt"
+    BENCH_RESULTS_FILE="${BENCH_RESULTS_DIR}/osc_latency.txt"
 )
```

CI then passes `-DBENCH_RESULTS_DIR=${{ runner.temp }}/bench` and uploads that
directory as an artifact. Default behaviour for local developers is unchanged.

### 3. `.github/workflows/ci.yml` — stages 1–4

Replaces `tests.yml`. **Keep the filename decision from §Decisions in mind
before deleting `tests.yml`.** Structure (SPDX header as in the existing
workflows):

```yaml
name: CI
on:
  push:
    branches: [main, rc_1]
  pull_request:
    types: [opened, synchronize, reopened, ready_for_review]
  workflow_dispatch:

concurrency:
  group: ci-${{ github.ref }}
  cancel-in-progress: true

permissions:
  contents: read
```

**Job `test`** (gating) — `ubuntu-latest`:
- `actions/checkout@v4` with `submodules: recursive`
- apt: `build-essential cmake pkg-config librtmidi-dev liblo-dev nlohmann-json3-dev libtinyxml2-dev`
- `ccache` via `hendrikmuhs/ccache-action@v1` keyed on `${{ runner.os }}-ci`
- `sudo modprobe snd-seq snd-seq-dummy || true` (keep the `|| true` from
  `tests.yml:52` — Azure kernels lack the modules; commit `0c2af7b` fixed
  precisely this)
- configure `-DCMAKE_BUILD_TYPE=Debug`, build, then
  `ctest --test-dir build --output-on-failure -LE bench`

**Job `bench`** (non-gating) — `needs: test`, `continue-on-error: true`:
- same setup, `-DBENCH_RESULTS_DIR=${{ runner.temp }}/bench`
- `ctest --test-dir build --output-on-failure -L bench`
- `actions/upload-artifact@v4` for the results dir, `if: always()`
- Rationale: a p99 miss on a shared runner is not a defect in this daemon, but
  the trend data is worth keeping. Do **not** let this job block the pipeline.

**Job `coverage`** — `needs: test`:
- lift the lcov + Codecov steps verbatim from `tests.yml:56-77` (the
  `--ignore-errors inconsistent` flags and the `/usr/*`, `*/tests/*`,
  `*/mtcreceiver/*`, `*/cuemslogger/*` exclusions are already tuned — do not
  re-tune them here)
- run coverage over `-LE bench` only, so the flaky benches cannot fail the job
- keep `fail_ci_if_error: false` and the existing `CODECOV_TOKEN` secret

**Job `docs`** — `needs: test`:
- apt `doxygen`, `pip install mkdocs==1.6.1 mkdocs-material mkdoxy` (pin as
  `docs.yml:44` already does)
- `mkdocs build` — with `DOXYGEN_WARN_AS_ERROR YES` this fails on any
  undocumented symbol, which is the intended DOCUMENT gate
- upload the site as an artifact; **do not deploy to Pages here** — Pages
  deployment stays in `docs.yml` (see §Decisions)

**Job `package`** — `needs: [coverage, docs]`, `container: debian:bookworm`
(constraint 7):
- `apt-get install -y --no-install-recommends build-essential devscripts
  debhelper cmake pkg-config librtmidi-dev liblo-dev nlohmann-json3-dev
  libtinyxml2-dev git ca-certificates`
- checkout with `submodules: recursive` (inside a container, `git` must be
  installed *before* checkout for submodules to work — install it in a
  pre-step or use `actions/checkout@v4`'s bundled fallback)
- **version-consistency guard** (see §4 below) — run this *before* building
- `dpkg-buildpackage -b -us -uc`
- move `../*.deb ../*.changes ../*.buildinfo` into `artifacts/` and upload

Because commit `9f1e0be` landed, this job needs no test-related workarounds:
`override_dh_auto_test` already skips the suite, `-DBUILD_TESTS=OFF` is now a
real option so the suite is not even compiled, and the submodule guard in
`override_dh_auto_configure` produces a legible error if checkout misbehaves.

### 4. Version-consistency guard

A shell step in `package` (and reused by DEPLOY) asserting the three sources of
truth agree — this is the drift T069 §6 had to reconcile by hand:

```bash
cmake_ver=$(grep -oP 'project\(gradient-motion-engine VERSION \K[0-9.]+' CMakeLists.txt)
deb_ver=$(dpkg-parsechangelog -S Version | cut -d- -f1)
chg_ver=$(grep -oP '^## \[\K[0-9.]+' CHANGELOG.md | head -1)
test "$cmake_ver" = "$deb_ver" && test "$cmake_ver" = "$chg_ver" || {
  echo "Version drift: CMakeLists=$cmake_ver debian/changelog=$deb_ver CHANGELOG=$chg_ver"; exit 1; }
```

On a tag build, additionally assert `"v$cmake_ver" = "$GITHUB_REF_NAME"`.

### 5. `.github/workflows/release.yml` — stage 5 (DEPLOY)

```yaml
on:
  push:
    tags: ['v*']
permissions:
  contents: write        # required to create/attach to the Release
```

- Reuse the `package` job (extract it into a reusable workflow called with
  `workflow_call` from both `ci.yml` and `release.yml`, so the release artifact
  is built by identical steps to the ones CI exercises on every push).
- `softprops/action-gh-release@v2` attaching `*.deb`, `*.changes`,
  `*.buildinfo`, with the body generated from the matching `CHANGELOG.md`
  section.
- Guard: refuse to publish if the version guard (§4) fails, and if the tag is
  not on `main` or `rc_1`.

### 6. Badges (the DOCUMENT deliverable)

`README.md:14-17` currently carries License, Tests, codecov, and Docs badges.
Update/extend to match the new suite:

| Badge | Source |
|---|---|
| License GPLv3 | unchanged (`README.md:14`) |
| CI | `actions/workflows/ci.yml/badge.svg` — **replaces** the `tests.yml` badge |
| codecov | unchanged — already token-pinned (`README.md:16`) |
| Docs | unchanged (`docs.yml`) |
| Release | `https://img.shields.io/github/v/release/stagesoft/gradient-motion-engine` |
| Debian package | `https://img.shields.io/github/v/tag/...?label=deb` or a shields endpoint badge fed by the release asset name |

Note `cuems-engine` renders its coverage badge via `genbadge` pushed to
`gh-pages/badges` (`ci.yml:112-125` there) because it has no Codecov token;
this repo *does* have one (`README.md:16`), so use the Codecov badge and do
**not** replicate the genbadge/gh-pages mechanism.

---

## Decisions required before implementation

1. **DEPLOY target.** No APT repository infrastructure exists anywhere in the
   ecosystem checkout (searched for `reprepro`/`aptly`/`packagecloud`/
   `cloudsmith`/`deb.cuems` — no hits). This plan therefore assumes **GitHub
   Releases as the artifact of record**, with fleet nodes installing via
   `dpkg -i` of a downloaded asset, mirroring how `cuems-engine` publishes to
   PyPI on `release: published`. If a fleet APT repo is intended instead
   (`reprepro` on a Stagelab host, signed with a repo key), stage 5 changes
   shape: it needs a GPG signing secret, `dput`/`reprepro includedeb`, and the
   `-us -uc` flags in `debian/rules` invocations become real signing. **Confirm
   which before implementing stage 5**; stages 1–4 are unaffected either way.
2. **Workflow filename.** `README.md:15` badge is pinned to `tests.yml`.
   Either (a) name the new pipeline `tests.yml` to preserve the badge and the
   branch-protection rule that likely references the "Tests" check, or
   (b) name it `ci.yml` (matching `cuems-engine`) and update both the README
   badge *and* any required-status-check config in repo settings in the same
   change. Recommendation: **(b)**, for ecosystem consistency — but it requires
   a settings touch that CI cannot do for itself.
3. **Pages ownership.** `docs.yml` owns the `pages` concurrency group and the
   `github-pages` environment. Keep it as the sole deployer (this plan's `docs`
   job only validates and uploads an artifact) to avoid two workflows racing
   for the same environment.

---

## Verification checklist

- [ ] `ctest -N -LE bench` reports 7 tests; `-L bench` reports 2 (§1).
- [ ] `cmake -B build -DBENCH_RESULTS_DIR=/tmp/b && ctest -L bench` leaves
      `git status` clean (§2).
- [ ] Open a throwaway PR: `test` gates, `bench` runs red-but-non-blocking,
      `coverage` and `docs` run in parallel, `package` produces a `.deb`
      artifact downloadable from the run summary.
- [ ] `dpkg-deb -I` on the CI-produced `.deb` shows `Version: 0.3.1-1` and
      Debian-flavoured `Depends:` — compare against the locally built package
      to confirm constraint 7 is actually addressed.
- [ ] Deliberately bump only `CMakeLists.txt` and confirm the version guard
      fails the `package` job (§4). Revert.
- [ ] Push tag `v0.3.1-test` on a scratch branch → release workflow attaches
      assets → delete the test release and tag.
- [ ] All README badges render green after merge (§6).

## Rollback

Purely additive to `.github/workflows/` plus two `tests/CMakeLists.txt` label
and path edits. Revert the merge commit; nothing in the daemon, the library, or
the package contents changes. The only externally visible state is any GitHub
Release created by a real tag push — delete the release and tag if a bad one
ships.

---

## Related artifacts

| Artifact | Location |
|---|---|
| Runner-safe packaging (prerequisite) | commit `9f1e0be`, `debian/rules`, `CMakeLists.txt:41-46` |
| Existing test workflow | `.github/workflows/tests.yml` |
| Existing docs workflow | `.github/workflows/docs.yml` |
| Ecosystem CI reference (lint→test→badge) | `cuems-engine/.github/workflows/ci.yml` |
| Ecosystem publish reference (release-triggered) | `cuems-engine/.github/workflows/pypi-publish.yml` |
| Test labels / bench paths | `tests/CMakeLists.txt:17,36,51,68,91,112,133,154,158,179` |
| Package metadata | `debian/control`, `debian/changelog` |
| Badges | `README.md:14-17` |
| Prior task | [T069](T069-fix-osc-send-failure-misclassification.md) |
