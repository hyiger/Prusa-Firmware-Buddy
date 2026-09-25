---
name: pre-pr-check
description: Reproduce the Prusa "Holly" Jenkins CI gates locally before pushing or opening/updating a Buddy firmware PR - fixup-commit check, prek formatting (clang-format 16, yapf, cmake-format, forbid-libc-assert, generated docs/presets), commit-message conventions, -Werror firmware builds of affected presets, and the unit-test suite. Use before committing/pushing, when asked "is this ready for review", or when CI failed and you need to reproduce it.
---

# Pre-PR checks (mirror of `utils/holly/build-pr.jenkins`)

Set the base first: `BASE=origin/master` for the public repo or a fork. Prusa's internal repo uses `private`. Run `git fetch origin master` before any of the checks.

## 1. Commit hygiene

```bash
git log --pretty=format:'%s' $BASE..HEAD --grep='^fixup!'     # CI fails the merge if anything is printed
git log --format='%h %s' $BASE..HEAD                           # eyeball subjects
```

Check every commit against `doc/contributing.md`:
- **Subject:** `module: Imperative capitalized subject`, with a lowercase module (e.g. `gui:`, `connect:`, `persistent_stores:`, `nozzle_cleaner_lite:`, `M865:`). No trailing period, 72 characters at most.
- **Body:** wrapped at 72 characters and explains *why*. A `BFW-xxxx` reference goes at the **end of the body**, never in the subject.
- **Atomicity:** each commit is one logical change and builds on its own (reviewers go commit by commit).
- **Subrepos:** commits that run `git subrepo pull/push` stay separate and are never squashed or rebased away (`doc/subrepo.md`).
- **Fixups:** they are fine during review. The author squashes them before the rebase-merge.

## 2. Formatting and generated files (CI stage "Check Formatting")

```bash
source .venv/bin/activate      # from utils/bootstrap.py; provides prek and the pinned clang-format 16 in .dependencies/
prek run -c .pre-commit-config.yaml --source $BASE --origin HEAD --show-diff-on-failure --hook-stage manual
```

- **What the hooks do:** they modify files in place. Review the diff and fold it into the right commit (`git commit --fixup=<sha>`, or amend if the branch is yours and unpublished).
- **clang-format version:** without bootstrap, a system clang-format (e.g. 18) **will** produce different output. Don't reformat files with it; report that formatting couldn't be verified instead.
- **Hook contents:**
  - clang-format 16 (C/C++)
  - yapf (Python)
  - cmake-format
  - trailing whitespace, EOF and line endings
  - `forbid-libc-assert`: rewrites `assert(` to `debug_assert(` and adds `#include <bsod/bsod.h>`. Opt out on a line with `// libc-assert-allowed`.
  - `generate-log-components-overview`: rewrites `doc/logging_components.md` when `LOG_COMPONENT_DEF` changes.
  - `generate-cmake-presets`: rewrites `CMakePresets.json` when a JSON file changes.
- **Scope:** vendored libraries under `lib/` are excluded, and in `lib/Marlin` only an allow-list of Prusa paths is formatted. Don't reformat code outside that list.

## 3. Unit tests (CI stage "Unit Tests")

```bash
python3 utils/build_tests.py --run -- --output-on-failure
```

Needs Python ≥ 3.12, the `requirements.txt` packages and gettext. See the `unit-tests` skill.

## 4. Firmware builds with `-Werror` (CI stage "Build")

CI builds these release, bootloader-enabled presets on every PR:

```
coreonel_indx coreone_indx coreone coreonel mini-en-cs mini-en-pl mk3.5 mk4 xl ix slx-anfc anfc  (+ anfc-uart noboot)
```

Branch builds also cover the other `mini-en-*` languages, `coreone_oak`, `xl-burst` and `xl-minimal`. Locally, build the subset your change can affect (see the `build-firmware` skill); when in doubt, build `mini-en-pl mk4 xl coreone coreonel_indx`:

```bash
python3 utils/build.py --preset mini-en-pl,mk4,xl,coreone,coreonel_indx --build-type release --bootloader yes \
    --skip-bootstrap -DCUSTOM_COMPILE_OPTIONS:STRING="-Werror" -DDEVELOPMENT_ITEMS_ENABLED:BOOL=YES
```

- **Release branches** (`RELEASE-*`) build with `DEVELOPMENT_ITEMS_ENABLED=NO`. If you touched `#if DEVELOPMENT_ITEMS()` code, build once with `NO` as well.
- **Option-gated changes** need a build with the option off too: `xl-minimal`, or `-DHAS_X=NO`.

## 5. Self-review checklist (things CI can't catch)

- **Config store:** no changed or reused `journal::hash` names, no deleted items, no changed defaults (see `config-store`).
- **Persisted enums and arrays:** nothing reordered or removed (filament types, extended printer types, selftest results, footer items).
- **Threading:** GUI code doesn't touch Marlin state directly, and nothing blocks the Marlin task except through `idle()`.
- **Memory:** no new heap allocations in steady state, and no large stack objects in small tasks. Watch MINI flash.
- **Strings:** new UI strings are wrapped in `_()` / `N_()`. English text isn't changed without a reason, because that drops translations.
- **G-codes:** new G-codes carry the doc comment format from `doc/contributing.md`.
- **Comments:** they explain *why*, contain no history ("changed X to Y"), and don't restate the code (`doc/contributing.md` § Comments).
- **Headers:** no author or copyright headers in new files, and `#pragma once` in headers.
- **Warnings:** `-Werror` builds reject new warnings. Watch for unused variables or functions in `#if` branches that are turned off.

## Reporting

Summarize each gate as passed, failed or not run (with the reason, e.g. "toolchain unavailable", "clang-format 16 not installed"). Never claim a gate passed if it wasn't run.
