---
name: build-firmware
description: Build Prusa Buddy firmware for one or more printer presets (mini, mk4, mk3.5, xl, coreone, coreonel, *_indx, ix, puppies) with utils/build.py, including bootstrap/toolchain setup, bootloader variants, incremental rebuilds, CI-equivalent -Werror builds, and choosing which presets a change must be compiled for. Use whenever asked to build, compile, check that a change compiles, fix a build error, check flash size, or produce a .bbf/.bin.
---

# Building the firmware

## 1. Prerequisites (one time)

```bash
python3.12 -m pip install --user requests   # bootstrap.py downloads with requests
python3.12 utils/bootstrap.py               # -> .dependencies/ (toolchain, cmake, ninja, clang-format 16, bootloaders) + .venv/
```

- **Python version:** use Python ≥ 3.12 so the `.venv` it creates can also run `utils/build_tests.py`.
- **Hosts it downloads from:** github.com, developer.arm.com (the ARM GCC 13.3.1 tarball), `prusa-buddy-firmware-dependencies.s3.eu-central-1.amazonaws.com`, and PyPI.
- **If a host is blocked** (proxy 403), report which host and stop. Do **not** substitute a distro `arm-none-eabi-gcc`. Ubuntu 24.04's 13.2 build fails on `PRId64` in `src/common/metric_handlers.cpp` with its newlib-nano, and code size differs too. Only the pinned toolchain is supported.
- **When it's done:** `.dependencies/gcc-arm-none-eabi-13.3.1/` exists, and `build.py` re-execs itself inside `.venv` automatically.

## 2. Build

```bash
python3 utils/build.py --preset <preset>[,<preset>...] --build-type <debug|release> --bootloader <yes|no> [--skip-bootstrap]
```

- **Presets:** the names are in `utils/presets/presets.json`; `python3 utils/build.py --help` lists them. The default is *all* presets, which is very slow, so always pass `--preset`.
- **`--bootloader`:**
  - `yes` links for the Prusa bootloader and produces a flashable `.bbf`. That is what users flash from USB, and what CI builds.
  - `no` links at the start of flash, for debugging with a probe (OpenOCD/ST-Link).
  - The default is `yes,no`, which builds both.
- **Output:**
  - The build dir is `build/<preset>_<type>_<boot|noboot>/`.
  - Products are `build/products/<preset>_<type>_<boot|noboot>.{bin,bbf,elf,map}`.
  - The version gets a `+<commit count>.LOCAL` suffix unless you pass `--final`.
- **Incremental rebuild** after the first configure: `.dependencies/ninja-1.10.2/ninja -C build/<cfg>`. This is much faster than rerunning `build.py`.
- **Configure only**, e.g. to get `compile_commands.json` for clangd: add `--no-build`, then symlink `build/<cfg>/compile_commands.json` to the repo root.
- **Extra CMake variables:** `-D NAME:TYPE=VALUE`, repeatable, e.g. `-DHAS_SELFTEST:BOOL=NO`, `-DDEVELOPMENT_ITEMS_ENABLED:BOOL=NO`.

### Puppies and master boards

- **Master boards** (`xl`, `coreone*`, `coreonel*`, `ix`) build their puppy firmware through ExternalProjects inside the build dir. Those projects use the presets `<printer>-<puppy>_<type>_boot`, so the first build is long.
  - `-DDWARF_BINARY_PATH=/path.bin` (or `MODULARBED_`, `XBUDDY_EXTENSION_`, …) reuses a prebuilt puppy binary.
  - `-DENABLE_PUPPY_BOOTLOAD=NO` drops puppy flashing entirely.
- **Puppy firmware alone:** build the puppy preset directly, e.g. `--preset xl-dwarf`.

## 3. Which presets must a change compile for?

A change often compiles for one printer and breaks another through `#if HAS_X()` or `PRINTER_IS_*` branches. Pick presets by what you touched:

| Touched | Build at least |
|---|---|
| common code (`src/common`, `src/gui`, `lib/Marlin` core, `src/persistent_stores`) | `mini` (small display, tightest flash), `mk4`, `xl`, `coreone` |
| MINI or small-display (`HAS_MINI_DISPLAY`) paths | `mini-en-pl` (largest image, closest to the flash limit) |
| toolchanger, puppies, Dwarf | `xl`, `xl-dwarf`, plus `coreone_indx` for INDX paths |
| xBuddy Extension, CORE One L, Cyphal/CAN | `coreone`, `coreonel`, `coreonel-xbuddy_extension`, `anfc` |
| option-gated code | one preset where the option is ON and one where it is OFF (`xl-minimal` turns many features off) |

CI (`utils/holly/build-pr.jenkins`) builds these release+boot presets on every PR:
`coreonel_indx coreone_indx coreone coreonel mini-en-cs mini-en-pl mk3.5 mk4 xl ix slx-anfc anfc anfc-uart(noboot)`.

## 4. CI-equivalent build

Warnings are errors in CI:

```bash
python3 utils/build.py --preset mk4 --build-type release --bootloader yes --skip-bootstrap \
    -DCUSTOM_COMPILE_OPTIONS:STRING="-Werror"
```

## 5. Size and diagnostics

- **Size:** `.dependencies/gcc-arm-none-eabi-13.3.1/bin/arm-none-eabi-size build/<cfg>/firmware`.
  - If the linker reports a region overflow on MINI, shrink the code: move strings to `N_()` constants, drop debug-only code behind `DEVELOPMENT_ITEMS()`, or avoid templates that instantiate per type.
  - `doc/puncover.md` covers a per-symbol breakdown. `utils/persistent_stores/config_store_analysis.py build/<cfg>/firmware` covers config-store sizes.
- **Include paths:** `utils/what_is_included.sh build/<cfg>/compile_commands.json <file>` shows the `-I` paths used for a source file.
- **Generated headers** such as `option/has_*.h`, `error_codes.hpp`, `qoi_resources.gen` and `gen_journal_hashes.hpp` live under `build/<cfg>/`. Look there when a symbol "doesn't exist" in the source tree.

## 6. Common failures

| Symptom | Cause / fix |
|---|---|
| `Python3 not found.` | There is no `.venv`: run bootstrap, or set `BUDDY_NO_VIRTUALENV=1` with a Python that has `requirements.txt` installed. |
| `Nnvg not found` | `pip install nunavut` (from `requirements.txt`). |
| `hash colision` | A new `journal::hash("...")` name collides. Rename it (see the `config-store` skill). |
| A preset is missing after editing `presets.json` | Run `python3 utils/build.py --generate-cmake-presets`. |
| `#if HAS_X()` preprocessor error | The `#include <option/has_x.h>` is missing. |
| `static_assert` on a screen or dialog size | The object outgrew the static buffer in `ScreenFactory` / `DialogHandler`. Shrink the object rather than growing the buffer, or discuss before growing it. |
