# Prusa Buddy Firmware: codebase guide

Firmware for the Original Prusa 32-bit printers: MINI/MINI+, MK3.5(S), MK3.9(S), MK4(S), XL, CORE One(+/L) and iX. It also covers the "puppy" MCUs those printers talk to (Dwarf tool heads, modular bed, xBuddy Extension, INDX head, NFC reader, tool-offset sensor).

- **Languages and platforms:** C++23 on STM32 (F407/F427/G070/H503/C092) with FreeRTOS. Motion comes from a heavily modified Marlin 2 fork. Build tooling is CMake + Ninja + Python.
- **Companion files:** `.claude/CLAUDE.md` (upstream-maintained; review rules, pulls in `doc/contributing.md`) is loaded alongside this file. Follow both.
- **Branches:** "`private`" in `.claude/CLAUDE.md` is Prusa's internal repo. The public repo and forks use **`master`**.
- **Skills:** task recipes live in `.claude/skills/` (see the last section).

## Commands

**Cloud sessions** (Claude Code on the web): `.claude/hooks/session-start.sh` prepares the container before the session starts.
- It installs gettext and creates `.venv` (Python ≥ 3.12, `requirements.txt`), which it puts on `PATH`.
- It fetches the pinned tools from `utils/bootstrap.py` that the network allows. Its output says what is missing, e.g. the ARM toolchain when `developer.arm.com` is blocked.

### Unit tests (host GCC, no ARM toolchain needed)

`build_tests.py` gotchas:
- It needs **Python ≥ 3.12**, because it uses PEP 701 f-strings. Under 3.11 it fails with `SyntaxError`.
- It imports `tree_sitter` / `tree_sitter_cpp` at the top of the script.
- Code generators need the packages in `requirements.txt`: `nunavut` (for `nnvg`), `polib`, `pyyaml`, `pillow`, `cbor2`, `ndeflib`, `simple_parsing`.
- The translator tests need gettext's `msgfmt`.

```bash
python3 utils/build_tests.py --run -- -LE slow        # build all tests, then run everything except [slow]
python3 utils/build_tests.py cobs_tests               # build only named targets (see --list)
python3 utils/build_tests.py -t -- -R "COBS"          # run only, no rebuild; args after -- go to ctest
# Manual equivalent (build dir defaults to build/tests):
cmake -S . -B build/tests -G Ninja -DBOARD=BUDDY && ninja -C build/tests tests
ctest --test-dir build/tests -LE slow --output-on-failure
```

- **Test names:** ctest names are the Catch2 `TEST_CASE` names, not the CMake target names. `-R` is a case-sensitive regex. Catch2 tags become ctest labels, so use `-L <tag>` / `-LE slow`.
- **Python lookup:** CMake looks for Python only in `<repo>/.venv`, unless a venv is active or `BUDDY_NO_VIRTUALENV=1` is set.

### Firmware (ARM)

`python3 utils/bootstrap.py` downloads the pinned tools into `.dependencies/`:
- cmake 3.28.3, ninja 1.10.2
- gcc-arm-none-eabi 13.3.1 (from `developer.arm.com`)
- clang-format 16
- prebuilt bootloaders, MMU firmware, the mini404 simulator

It also creates `.venv/` from `requirements.txt` and installs the prek git hooks. Run it with Python ≥ 3.12 and with `requests` installed.

```bash
python3 utils/build.py --preset mk4 --build-type debug --bootloader no     # -> build/mk4_debug_noboot/
python3 utils/build.py --preset mini,xl --build-type release               # default --bootloader yes,no builds both
python3 utils/build.py --preset coreone --no-build                          # configure only (compile_commands.json)
python3 utils/build.py ... --skip-bootstrap --toolchain cmake/AnyGccArmNoneEabi.cmake   # arm-none-eabi-gcc from PATH (unsupported, see below)
```

Only the pinned ARM toolchain is supported. Distro toolchains are not a drop-in replacement: for example, Ubuntu 24.04's `gcc-arm-none-eabi` 13.2 fails on `PRId64` with its newlib-nano.

- **Outputs:** products go to `build/products/<preset>_<type>_<boot|noboot>.{bin,bbf,elf,map}`. The build dir is `build/<preset>_<type>_<boot|noboot|emptyboot>`.
- **Configs:** `CMakePresets.json` is **generated** from `utils/presets/presets.json`. Edit the latter, then run `python utils/build.py --generate-cmake-presets` (a prek hook does this too).
- **CI:** CI builds release presets with `-DCUSTOM_COMPILE_OPTIONS:STRING="-Werror"`, so warnings are errors. See `utils/holly/build-pr.jenkins`.
- **Version suffix:** `<auto>` (`+<commit count>.LOCAL`) by default; `--final` removes it.

### Formatting and hooks

Run `prek run -c .pre-commit-config.yaml` (add `--all-files` to check everything).
- CI checks the PR range with the same config.
- The pinned **clang-format 16** is in `.dependencies/`. A system clang-format of another version formats differently.
- Other hooks: yapf (Python), cmake-format, `forbid-libc-assert`, and the regeneration of `doc/logging_components.md` and `CMakePresets.json`.
- Most of `lib/` is excluded. Inside `lib/Marlin` only an allow-list of Prusa paths is formatted.

## Printers, presets, boards

| Preset | `PRINTER` | Board (MCU) | Notes |
|---|---|---|---|
| `mini`, `mini-en-<lang>` | MINI | BUDDY (F407) | 240×320 ST7789 display; flash-constrained (CI always builds `mini-en-pl`, largest) |
| `mk4` | MK4 | XBUDDY (F427) | one image for MK4/MK4S/MK3.9/MK3.9S (runtime `ExtendedPrinterType`) |
| `mk3.5` | MK3.5 | XBUDDY | MK3.5/MK3.5S |
| `xl`, `xl-minimal`, `xl-burst` | XL | XLBUDDY (F427) | XL/XLS; toolchanger; puppies over Modbus |
| `coreone`, `coreone_oak` | COREONE | XBUDDY | + xBuddy Extension (XBE); `coreone_oak` = `SIGNATURE_OAK` branding build |
| `coreonel` | COREONEL | XBUDDY | AC bed controller via XBE → Cyphal/CAN (`doc/core-one-l/`) |
| `coreone_indx`, `coreonel_indx` | *_INDX | XBUDDY | INDX toolchanger |
| `ix` | iX | XBUDDY | separate release channel; leave iX-only changes out of public release notes |
| `xl-dwarf`, `*-modularbed` | XL/iX | DWARF/MODULARBED (G070) | puppies |
| `*-xbuddy_extension`, `xl-can` | … | XBUDDY_EXTENSION (H503) | puppy |
| `*anfc*`, `*-tool_offset_sensor`, `*-indx_head` | … | ANFC / TOOL_OFFSET_SENSOR / INDX_HEAD (C092) | Cyphal puppies |
| `unittests_*` | — | host | `UNITTESTS_ENABLE` |

- **Puppy firmware:** a master-board build builds its puppy firmware through `add_puppy_firmware()` (ExternalProject with preset `<printer>-<puppy>_<type>_boot`).
  - `-D<PUPPY>_BINARY_PATH=` uses a prebuilt binary instead.
  - `-DENABLE_PUPPY_BOOTLOAD=NO` skips puppy flashing.
- **Bootloader:** it is not built here. Prebuilt binaries come from `.dependencies/bootloader-*`.

## Compile-time configuration (critical idioms)

- **`HAS_*` feature flags** are defined in `ProjectOptions.cmake`:
  - `set_feature_for_printers(HAS_X "MK4" "COREONE" ...)`; the `_master_board` variant forces NO on puppies. Add `"UNITTESTS"` to the list to enable the flag in unit tests.
  - `-DHAS_X=NO` on the command line overrides the list (`xl-minimal` does this).
  - Each flag becomes both a CMake variable (`if(HAS_X)`) and a generated header `build/.../include/option/has_x.h` (`cmake/Options.cmake`).
- **Using a flag in C++:** `#include <option/has_x.h>`, then `#if HAS_X()` or `if constexpr (option::has_x)`.
  - **Never** `#ifdef HAS_X`: the macro is always defined, as 0 or 1.
  - A missing include makes `#if HAS_X()` a preprocessor error, which is intentional.
  - Enum options (`define_enum_option`) generate `NAME_IS_<V>()` and `option::name`.
- **Printer and board checks:**
  - `#include <printers.h>` for `PRINTER_IS_PRUSA_MK4()`, `_MINI()`, `_XL()`, `_COREONE()`, `_COREONEL()`, `_MK3_5()`, `_iX()`. COREONE also covers COREONE_INDX; XL_DEV_KIT also counts as XL.
  - `#include <device/board.h>` for `BOARD_IS_XBUDDY()` etc.
  - `include/guiconfig/guiconfig.h` for `HAS_MINI_DISPLAY()` / `HAS_LARGE_DISPLAY()`.
  - Prefer a `HAS_*` feature flag over a printer check when the code is about a capability.
- **Printer identity:** `PrinterModelInfo` (`include/common/printer_model.hpp`) and `extended_printer_type.hpp`. Order-sensitive arrays there are persisted, so never reorder them.
- **Marlin config:** `include/marlin/Configuration_<PRINTER>.h` and `_adv.h`. Runtime or EEPROM-dependent values go in `src/marlin_stubs/<PRINTER>/configuration.hpp`.

## Source map

| Path | What |
|---|---|
| `src/buddy/main.cpp` | `main()`, startup task, creation of all main tasks |
| `src/common/` | the bulk of the firmware: `marlin_server*`/`marlin_client*`, FSM, print flow, selftest, loadcell, filament types, hwio, power panic, crash dump, `gcode/` (parsers, readers, injection) |
| `src/gui/`, `src/guiapi/` | app screens, menus (`MItem_*`, `screen_menu_*`), dialogs, footer / window toolkit, display drivers |
| `src/marlin_stubs/` | Prusa G-codes (`PrusaGcodeSuite`), overrides of Marlin G-codes, `pause/`, per-printer `configuration.hpp` |
| `src/feature/<name>/` | printer features, each gated by `HAS_*` in `src/feature/CMakeLists.txt` |
| `src/module/<name>/` | standalone `add_library` modules only (no `firmware` sources): modbus, fanctl, nfc, utils (bsod, buddy_utils), raii, coding, i2c/spi drivers… |
| `src/persistent_stores/` | `config_store` (journaled EEPROM settings) |
| `src/connect/`, `lib/WUI/`, `src/transfers/` | Prusa Connect client; LwIP networking, PrusaLink HTTP server (`nhttp`, `link_content`); downloads/partial files |
| `src/puppies/` | printer-side drivers for puppies (Modbus master, bootstrap/flash, Dwarf, XBE, INDX, Cyphal bridge) |
| `src/puppy/<board>/` | puppy firmware itself (dwarf, modularbed, xbuddy_extension, indx_head, anfc, tool_offset_sensor) |
| `src/can/` | Cyphal/libcanard; `data_types/public_regulated_data_types` is upstream, never edit; custom DSDL in `prusa3d/` |
| `src/mmu2/` + `lib/Marlin/.../feature/prusa/MMU2/` | MMU2S/MMU3 integration (protocol shared with MK3 FW) |
| `src/lang/`, `src/gui/res/`, `src/resources/` | i18n (`po/`), images (PNG→QOI), resources tarball packed into `.bbf` |
| `src/logging/`, `src/common/metric*` | logging and metrics |
| `lib/Marlin/Marlin/src/` | Marlin fork; Prusa parts in `feature/{prusa,precise_stepping,phase_stepping,input_shaper,pressure_advance}`, `module/prusa/` (toolchanger, homing, tool_mapper, spool_join), `module/temperature/` |
| `include/` | shared headers: `option/*.in`, `printers.h`, `marlin/Configuration_*.h`, `device/`, `guiconfig/`, `tasks.hpp` |
| `tests/unit/` | Catch2 tests, loosely mirroring `src/`; `tests/stubs` has shared fakes |
| `utils/` | build and bootstrap, CI (`holly/`), gdb/OpenOCD/crash-dump tools, translation pipeline, generators |
| `doc/` | contributing, logging, metrics, timers, TMC config, release-notes style, CORE One L electronics |

## Runtime architecture

- **Startup and tasks.** `main()` creates a startup task. It inits the EEPROM and config store, runs the C++ static constructors (`__libc_init_array` runs here, not before the scheduler), then `main_cpp()` creates the tasks with static stacks in CCMRAM.
  - Main tasks: `marlin` (`app_run` → `marlin_server::loop()`), `display` (`gui_run`), `puppies`, `network`, `connect`, `acfault` (power panic), plus log, USB, ESP flash and the async job executor.
  - Start-up ordering uses `TaskDeps` event bits (`include/tasks.hpp`): `TaskDeps::wait(...)` / `provide(...)`.
- **Marlin server and client.** Only the `marlin` task touches Marlin.
  - Other tasks go through `marlin_client` (`gcode()`, `inject(GCodeLiteral("..."))`, `print_start()`, `FSM_response()`, request flags).
  - They read shared state through `marlin_vars()`. A `MarlinVariable` is atomic and written only by the server; writing it from another task is a BSOD. Locked variables use `MarlinVarsLockGuard`.
  - `marlin_server::cycle()` runs from Marlin `idle()` via `ExtUI::onIdle()`, so it also runs inside blocking G-codes.
- **FSM dialogs.** Marlin-side code shows UI and waits for the user through FSMs:
  - `ClientFSM` (`client_fsm_types.hpp`), `Phases*` + response tables (`client_response.hpp`, `fsm/*_phases.hpp`), `Response` (`general_response.hpp`).
  - Server side: `marlin_server::FSM_Holder holder{Phase::x}; wait_for_response(Phase::x); fsm_change(...)`.
  - GUI side: `DialogHandler` maps each `ClientFSM` to a screen or dialog (`FSMScreenDef` / `FSMDialogDef`); screens derive from `ScreenFSM` with `FrameDefinition<Phase, Frame>`.
  - `ClientFSM` must stay below 32 entries.
- **GUI.**
  - Only one screen object exists at a time, built into a static buffer by `ScreenFactory`. `Screens::Access()->Open(ScreenFactory::Screen<X>)` opens a screen; the navigation stack keeps creators, not objects.
  - Menus: `ScreenMenu<EFooter, MI_...>` / `BasicScreenMenu<MI_...>`, with items `MI_UPPER_SNAKE` deriving `IWindowMenuItem` / `WI_ICON_SWITCH_OFF_ON_t` / `WiSpin` / `MI_SCREEN<N_("Label"), class ScreenX>`.
  - Blocking message boxes: `MsgBoxQuestion(_("..."), Responses_YesNo)`.
- **Print flow.** `print_start` → `PrintPreview` (`marlin_print_preview.cpp`) → `State::Printing`. G-code is streamed through `media_prefetch` (a ring buffer filled by the async executor) from `PlainGcodeReader` / `PrusaPackGcodeReader` (bgcode).
  - `marlin_server::State` covers pausing, resuming, aborting, crash recovery and power panic.
- **Errors.**
  - `bsod("fmt", ...)`, `bsod_unreachable()`, `debug_assert()`, `release_assert()` come from `<bsod/bsod.h>`.
  - Red screens with an error code: `fatal_error(ErrCode::ERR_..., ...)` (`src/common/bsod.h`).
  - Non-fatal warnings: `marlin_server::set_warning(WarningType::...)`.
  - Error codes are generated from `lib/Prusa-Error-Codes/yaml/buddy-error-codes.yaml`.

## How-to quick reference

- **Persistent setting:** add a `StoreItem<T, default, ItemFlag::..., journal::hash("Unique Name")>` to `CurrentStore` in `src/persistent_stores/store_instances/config_store/store_definition.hpp`. Read and write it with `config_store().x.get()` / `.set(v)`.
  - Never change or reuse a hash name. Never delete an item: deprecate it.
  - Changing a default counts as a deprecation.
  - See the `config-store` skill.
- **Logging:**
  - In exactly one `.cpp`, with the line starting at column 0: `LOG_COMPONENT_DEF(Name, logging::Severity::info);`
  - In other files: `LOG_COMPONENT_REF(Name);`, then `log_info(Name, "x=%d", x);`.
  - Levels: debug/info/warning/error/critical. `log_debug` compiles out of release builds.
  - Commit the regenerated `doc/logging_components.md` (the hook updates it).
- **Metrics:** `METRIC_DEF(var, "name", METRIC_VALUE_FLOAT, interval_ms, METRIC_DISABLED);` then `metric_record_float(&var, v);`. See `doc/metrics.md`.
- **UI strings:**
  - Translated text: `_("Text")`, which returns `string_view_utf8`.
  - Marked for translation but translated later: `N_("Text")`, e.g. `static constexpr const char *label = N_("...")` then `_(label)`.
  - Untranslated text: `string_view_utf8::MakeCPUFLASH(...)` / `MakeRAM(...)`.
  - Never hand-edit `src/lang/po/*`. Prusa staff regenerate them in "Update translations" commits.
  - Changing an English msgid drops existing translations until the next update.
  - Fonts contain only the glyphs the build needs. A new non-ASCII glyph may need font or replacement-table work.
- **Images:** put `src/gui/res/png/<name>_<W>x<H>.png` there, list it in `src/gui/res/<PRINTER>_used_imgs.txt`, and use `&img::<name>_<W>x<H>`. If the list entry is missing, the build fails at link time. See `doc/Pictures_in_FW.md`.
- **New G-code:** see the `add-gcode` skill.
  1. Declare it in `src/marlin_stubs/PrusaGcodeSuite.hpp`.
  2. Add a `case` in `src/marlin_stubs/gcode.cpp` (it is dispatched before Marlin's own handlers).
  3. Implement it in `src/marlin_stubs/Mxxxx.cpp`, parsing with `GCodeParser2`.
  4. Add the file to `src/marlin_stubs/CMakeLists.txt` and document it with the format in `doc/contributing.md`.
- **New source file:** add `target_sources(firmware PRIVATE foo.cpp)` in the directory's `CMakeLists.txt`, inside `if(HAS_X)` when it is feature-specific.
  - Files in Marlin go in `lib/AddMarlin.cmake`, which lists them by hand.
  - A new `src/feature/<name>/` directory needs `add_subdirectory` in `src/feature/CMakeLists.txt`.
- **New error code:** edit the YAML in the `lib/Prusa-Error-Codes` **subrepo**; `ErrCode::<id>` is generated from it.

## Rules and constraints

- **Language features:**
  - No exceptions and no RTTI (`-fno-exceptions -fno-rtti`; unit tests have RTTI).
  - Use `std::optional`, `std::expected` and return codes.
  - Replacements for RTTI: `include/common/no_rtti_type_id.hpp`, `primitive_any.hpp`, `stdext::inplace_function`.
- **Memory:**
  - Allocate statically. Widgets are member objects; screens and dialogs are placement-new'd into fixed buffers with `static_assert` size checks.
  - Avoid heap allocation in firmware code: running out of heap is a BSOD. `malloc_fallible` is available where failure is handled.
  - MINI flash and RAM are tight. Watch the size impact (`doc/puncover.md`, `utils/persistent_stores/config_store_analysis.py`).
- **`assert()`:** libc `assert()` is forbidden (pre-commit rewrites it). Use `debug_assert` / `release_assert` from `<bsod/bsod.h>`.
- **Threads:**
  - Respect task boundaries: GUI → `marlin_client`, never Marlin globals directly.
  - Server code blocks only through `idle()`-based waits.
  - `config_store().x.set()` locks a mutex, so never call it from an ISR. It writes to EEPROM, so avoid frequent writes (wear).
- **Utilities to reuse:**
  - `ticks_ms()` / `ticks_diff()` (`timing.h`)
  - `freertos::Mutex`, `Queue<T,N>`
  - `StringBuilder` / `ArrayStringBuilder<N>`
  - `EnumArray`, `ScopeGuard`, `AutoRestore`, `RateLimiter`
  - `str_utils.hpp`, `utility_extensions.hpp`
- **Subrepos:** `lib/Marlin`, `lib/Prusa-Error-Codes`, `lib/Prusa-Firmware-MMU`, LwIP, mbedtls, littlefs, the HAL drivers and others (anything with a `.gitrepo`) are git subrepos.
  - Editing `lib/Marlin` in normal feature commits is fine.
  - Never hand-edit `.gitrepo`, never hand-copy changes to or from upstream, and never rebase or squash commits that run `git subrepo pull/push` (`doc/subrepo.md`).
- **Generated files, don't edit:** `CMakePresets.json`, `doc/logging_components.md`, everything under `build/` (option headers, `gen_journal_hashes.hpp`, `error_codes.hpp`, `qoi_resources.gen`, fonts), and the `.po` files.
- **Commits:** `module: Imperative subject` (lowercase module, ≤72 characters, no trailing period), body wrapped at 72, `BFW-xxxx` at the end of the body if you have one.
  - Each commit must build on its own.
  - CI rejects `fixup!` commits at merge time.
  - Rebase-merge workflow; the user squashes fixups by hand.
- **Public PRs:** go against `master` (`doc/contributing/public_pr_workflow.md`).

## Testing guidance

- **Where to test:** logic that doesn't need hardware belongs in `tests/unit/<mirrored path>/`.
  - Compile the unit under test directly into an `add_executable`, then call `add_catch_test(target)`.
  - Stub heavy headers through `tests/stubs` or per-test `stub*/` include dirs placed first.
  - Tests run with `PRINTER=MINI` / `BOARD=BUDDY` defines. Features enabled for `"UNITTESTS"` are on.
- **Hardware-only code:** there is no hardware in CI. Firmware changes are validated by building every affected preset with `-Werror`, and XL or CORE One changes need their own builds.
- **Integration tests:** `tests/integration` (pytest + QEMU `mini404`, MK4 noboot only) is currently disabled in CI.

## Debugging

- **Debug builds:** `--build-type debug` enables `log_debug`, and the watchdog is disabled.
- **Probe and GDB:** OpenOCD configs are in `utils/debug/`, VS Code cortex-debug launch configs in `.vscode/launch.json`, and gdb helpers in `utils/gdb/` and `utils/freertos-gdb-plugin/`.
- **Crash dumps:** `utils/crash_dump_debug.py --dump <file> --elf build/<cfg>/firmware`.
- **Reference docs:** `doc/debugging_profiling.md`, `doc/timers.md` (hardware timer allocation), `doc/tmc_config.md`, `doc/core-one-l/pub6-debugging.md` (CAN).

## Skills (`.claude/skills/`)

| Skill | Use for |
|---|---|
| `build-firmware` | building one or more presets, toolchain problems, CI-equivalent `-Werror` builds |
| `unit-tests` | running, filtering, adding or debugging Catch2 unit tests |
| `config-store` | adding, deprecating or migrating persistent settings |
| `add-gcode` | implementing or modifying a Prusa-specific G-code |
| `feature-flag` | adding a `HAS_*` option or gating code per printer or board |
| `gui-menu` | adding menu items, settings screens and message boxes |
| `fsm-dialog` | Marlin-side flows that show a dialog or wizard and wait for user input |
| `pre-pr-check` | reproducing the Holly CI gates locally before pushing |
| `release-notes` | writing release notes per `doc/release_notes.md` |
