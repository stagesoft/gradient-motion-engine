<!--
SPDX-FileCopyrightText: 2026 Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing to gradient-motion-engine

Thank you for your interest in `gradient-motion-engine`. This daemon runs on every
CueMS player node and drives fades against MIDI Time Code in real time — a regression
here causes audible / visible artefacts at show time. These guidelines exist to
protect that reliability while keeping the project open to external contributions.

The authoritative governance document for the rules summarised here is
[`.specify/memory/constitution.md`](.specify/memory/constitution.md).
If this file and the constitution conflict, the constitution wins.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Development Setup](#2-development-setup)
3. [Contribution Tiers](#3-contribution-tiers)
4. [Branch Naming](#4-branch-naming)
5. [Spec-First Requirement](#5-spec-first-requirement)
6. [TDD Workflow — Non-Negotiable](#6-tdd-workflow--non-negotiable)
7. [Commit Hygiene](#7-commit-hygiene)
8. [Developer Certificate of Origin (DCO)](#8-developer-certificate-of-origin-dco)
9. [Pull Request Requirements](#9-pull-request-requirements)
10. [Acceptance Criteria](#10-acceptance-criteria)
11. [Review Process](#11-review-process)
12. [Changelog Line](#12-changelog-line)
13. [Dependency Governance](#13-dependency-governance)
14. [License](#14-license)

---

## 1. Prerequisites

| Tool | Version | Notes |
|---|---|---|
| C++ compiler | g++ ≥ 12 (C++17) | clang++ ≥ 14 also supported |
| CMake | ≥ 3.16 | newer is fine |
| pkg-config | any recent | required by the CMake config |
| Git | any recent | DCO sign-off required (see §8) |
| Doxygen + Graphviz | optional | only needed to build the HTML reference |
| lcov / gcov | optional | only needed to produce coverage locally |

System packages (Debian/Ubuntu):

```bash
sudo apt-get install -y \
  build-essential cmake pkg-config \
  librtmidi-dev liblo-dev nlohmann-json3-dev libtinyxml2-dev \
  doxygen graphviz lcov
```

The submodules are not optional for the daemon build (`mtcreceiver`, `cuemslogger`).
Either clone with `--recursive` or run `git submodule update --init --recursive`
after a normal clone.

---

## 2. Development Setup

```bash
# Clone with submodules
git clone --recursive https://github.com/stagesoft/gradient-motion-engine.git
cd gradient-motion-engine

# Configure (debug build, daemon enabled)
cmake -B build -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build -j$(nproc)

# Run the full test suite
ctest --test-dir build --output-on-failure
```

Lint (clang-tidy / clang-format must pass on every changed file):

```bash
# Format check (will exit non-zero if any tracked file would change)
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')

# Static analysis on changed files
clang-tidy -p build $(git diff --name-only main -- '*.cpp')
```

Library-only build (no MIDI, useful for embedding):

```bash
cmake -B build -DBUILD_DAEMON=OFF
cmake --build build
```

Coverage build:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="--coverage" \
  -DCMAKE_EXE_LINKER_FLAGS="--coverage"
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
lcov --capture --directory build --output-file coverage.info --ignore-errors inconsistent
lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
```

---

## 3. Contribution Tiers

The review requirements depend on what you change.

### Tier 1 — Trivial

No change to any file under `src/`, `daemon/`, or `tests/` beyond a single-line
correction. Covers: README edits, doc fixes, comment corrections, adding a test
for already-shipped behaviour, CI/CD config changes, packaging fixes.

**Gates**: lint + CI green; one owner approval. No spec required.

### Tier 2 — Non-trivial

Any addition, modification, or deletion of logic in `src/` or `daemon/`. Includes
bug fixes that change branching behaviour, new features, refactors, new class
introductions, and changes to the OSC wire contract.

**Gates**: spec + plan + tasks on the branch; failing test before implementation;
CI green (tests + coverage); constitution compliance declaration; one mandatory
owner approval.

---

## 4. Branch Naming

```
feat/NNN-short-description       ← new feature  (NNN = spec number, e.g. 007)
fix/NNN-short-description        ← bug fix referencing a spec or issue number
chore/short-description          ← non-production changes (CI, tooling, docs)
build/short-description          ← packaging / build-system changes
```

The `NNN` prefix links the branch to `specs/NNN-feature/` artifacts. Branches
without a valid prefix will not be merged.

---

## 5. Spec-First Requirement

For **Tier 2** changes, before opening a PR for review you MUST commit these
files on your feature branch:

```
specs/NNN-feature/spec.md      ← feature specification
specs/NNN-feature/plan.md      ← implementation plan with Constitution Check completed
specs/NNN-feature/tasks.md     ← task list (generated by /speckit-tasks)
```

If you are a first-time contributor unfamiliar with the spec format, open an
issue first and the maintainers will help you scope the work.

The PR description must link to the spec directory. A PR without it will be marked
as a draft and returned for pre-work.

The CueMS Spec Kit slash commands (`/speckit-specify`, `/speckit-plan`,
`/speckit-tasks`, `/speckit-analyze`) live in `.claude/commands/` and operate
against the artifacts under `specs/`.

---

## 6. TDD Workflow — Non-Negotiable

`gradient-motion-engine` enforces Test-Driven Development for all Tier 2 changes.
This is not a style preference — it is a constitutional requirement
(Principle V).

The mandatory sequence:

```
1. Write a failing test that precisely describes the intended behaviour.
2. Confirm CI fails on that commit (or run ctest locally and record the failure).
3. Write the minimum production code required to make the test pass.
4. Refactor without changing observable behaviour, keeping all tests green.
```

Your git log on the feature branch MUST show this order. The PR template asks for
the commit SHA of your failing-test commit. Reviewers will check it.

```bash
# Run the full suite
ctest --test-dir build --output-on-failure

# Run a single test (e.g., parser changes)
ctest --test-dir build -R test_osc_parse --output-on-failure

# Run only unit-labeled tests
ctest --test-dir build -L unit --output-on-failure

# End-to-end deploy tests against a running daemon (manual)
bash dev/deploy_tests/s007_t034_smoke.sh
```

Constitution Principle IV (Real-Time Safety) implies an additional gate: any
change to a code path that runs on the MTC tick callback MUST be accompanied by
either a microbenchmark in `tests/bench_*.cpp` or a documented argument for why
the change is heap-free, lock-free, and non-blocking.

---

## 7. Commit Hygiene

`gradient-motion-engine` uses [Conventional Commits](https://www.conventionalcommits.org/) v1.0.

```
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
Signed-off-by: Your Name <your@email.com>
```

Allowed types: `feat`, `fix`, `test`, `refactor`, `docs`, `chore`, `ci`, `perf`,
`build`, `patch`, `spec`.

Breaking changes: append `!` after the type and include a `BREAKING CHANGE:` footer.

Rules:
- Each commit MUST represent one logical change.
- Do not squash unrelated changes into a single commit.
- Force-pushing to `main` is forbidden. Amending published commits on shared
  branches is forbidden.
- Spec / plan / tasks commits use the `spec:` type prefix (see `git log`).

---

## 8. Developer Certificate of Origin (DCO)

Every commit must carry a `Signed-off-by` line, asserting that you have the right to
submit the contribution under GPL-3.0, as per the
[Developer Certificate of Origin](https://developercertificate.org).

```bash
git commit -s -m "feat: add support for ..."
```

To add sign-off to all commits in a branch automatically, set:

```bash
git config --local format.signOff true
```

PRs that contain unsigned commits will not be merged.

---

## 9. Pull Request Requirements

Open your PR against `main`. Use the PR template — it contains the full
acceptance checklist.

Every PR MUST include in its description:

1. **Summary** — what changed and why (2–5 sentences).
2. **Changelog Line** — see §12.
3. **Spec links** (Tier 2 only) — link to `specs/NNN-feature/`.
4. **Failing-test commit SHA** (Tier 2 only) — the commit where CI was red before
   implementation began.
5. **Tick-path impact statement** (Tier 2 only) — does the change touch a code
   path that runs on the MTC callback thread? If yes, link to the bench result
   or argue why the change is heap-free / non-blocking.
6. **Completed PR checklist** — all items in the template ticked.

Draft PRs are welcome for early feedback on approach. Drafts will not be formally
reviewed. Convert to Ready when all gates pass.

---

## 10. Acceptance Criteria

A PR is ready to merge when ALL of the following are true:

| Criterion | How verified |
|---|---|
| `spec.md`, `plan.md`, `tasks.md` committed on branch (Tier 2) | Reviewer reads the files |
| Failing test committed before implementation (Tier 2) | SHA provided; reviewer checks git log |
| All `ctest` targets pass | CI green |
| Coverage ≥ 75% on changed files | CI coverage gate (Codecov) |
| `clang-format --dry-run --Werror` passes on changed files | CI lint gate |
| `clang-tidy` reports no new warnings on changed files | CI lint gate |
| No new build-time dependency without justification | Reviewer checks `CMakeLists.txt` + `debian/control` diff |
| SPDX header on all new source files | Reviewer inspects new files |
| No exceptions thrown across the library boundary | Reviewer checks `noexcept` and return-type signatures |
| Tick-path changes accompanied by a bench or argument (Principle IV) | Reviewer reads PR body |
| DCO sign-off on all commits | GitHub DCO check |
| At least one owner approval | GitHub branch protection |

---

## 11. Review Process

All PRs to `main` require approval from at least one repository owner:

- **Ion Reguera** ([@ibiltari](https://github.com/ibiltari))
- **Adrià Masip** ([@backenv](https://github.com/backenv))

This is enforced by `.github/CODEOWNERS` and GitHub branch protection.

**What owners check:**
- Spec, plan, and tasks are coherent with the implementation (Tier 2).
- TDD sequence is evidenced in the git log.
- All CI gates pass.
- Constitution checklist is ticked accurately, not perfunctorily.
- No new runtime dependency slipped in without justification.
- SPDX header present on all new source files.
- Exceptions are not thrown across the library boundary; errors propagate as
  enum returns or `std::optional`.
- The MTC tick path remains heap-free, lock-free, and non-blocking (Principle IV).
- New motion types subclass `IMotion` and register through `MotionFactory`;
  they do not require changes to `MotionRegistry` (Principle VII, open/closed).
- SOLID principles respected — single responsibility, dependencies injected not
  constructed.

Expect review turnaround within 5 business days. For urgent fixes, open an issue
first and tag a maintainer — that speeds triage.

---

## 12. Changelog Line

You do not edit `CHANGELOG.md` — that is the maintainers' responsibility at release
time. Instead, include a **Changelog Line** in your PR description. Maintainers copy
this line verbatim (or lightly edited) when cutting a release.

Format:

```
[TYPE] Past-tense sentence describing what changed and why it matters to users.
```

Types: `Added`, `Changed`, `Fixed`, `Removed`, `Security`, `Performance`.

Examples:

```
[Added] FadeMotion supports the new "scurve" curve type via CurveFactory.
[Fixed] MotionRegistry no longer leaks lo_address handles on supersede.
[Changed] FadeCommand renamed fade_id → motion_id for ecosystem consistency.
[Removed] NNG bus client replaced by liblo UDP OSC listener.
```

---

## 13. Dependency Governance

No new entry under `find_package(...)` or `pkg_check_modules(...)` in
`CMakeLists.txt`, and no new entry under `Build-Depends:` / `Depends:` in
`debian/control`, may be introduced without:

1. A written justification in the PR description explaining why the standard
   library, the existing dependencies, and a header-only alternative cannot
   solve the problem.
2. Explicit acknowledgement from a repository owner in the review.

Current runtime dependencies (the link closure of `gradient-motiond`):

- `librtmidi-dev` — MIDI I/O for `mtcreceiver`
- `liblo-dev` — OSC transport (in and out)
- `nlohmann-json3-dev` — `curve_params_json` parsing
- `libtinyxml2-dev` — `settings.xml` parsing in `ConfigurationManager` (planned use)

Build-time additions for tests only (`tests/CMakeLists.txt`) are lower friction
but still require a one-line justification in the PR description.

Git submodules (`mtcreceiver`, `cuemslogger`) are tracked at pinned commits.
Bumping a submodule pin requires the same justification as adding a new dependency
plus a re-run of the full test suite against the new pin.

---

## 14. License

`gradient-motion-engine` is licensed under the GNU General Public License v3.0
(GPL-3.0). By contributing, you agree that your contributions will be licensed
under GPL-3.0.

All new source files MUST carry the following SPDX header:

```cpp
/*
 * ***
 * SPDX-FileCopyrightText: <year> Stagelab Coop SCCL
 * SPDX-License-Identifier: GPL-3.0-or-later
 * ***
 */
```

For Markdown files:

```markdown
<!--
SPDX-FileCopyrightText: <year> Stagelab Coop SCCL
SPDX-License-Identifier: GPL-3.0-or-later
-->
```

For CMake / shell / Python files:

```bash
# ***
# SPDX-FileCopyrightText: <year> Stagelab Coop SCCL
# SPDX-License-Identifier: GPL-3.0-or-later
# ***
```
