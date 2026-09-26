---
name: unit-tests
description: Build, run, filter, debug and add Catch2 unit tests for Prusa Buddy firmware (tests/unit, utils/build_tests.py, ctest). Use when asked to run tests, write a test for new or changed logic, reproduce a bug on the host, check that a refactor didn't break behaviour, or when a test build fails for environment reasons (Python version, missing nnvg/msgfmt/PIL).
---

# Unit tests

Unit tests are host-compiled with GCC; no ARM toolchain is needed. They use Catch2 v3 (vendored in `lib/Catch2`), C++23, RTTI on and `-DUNITTESTS`. The printer defines are MINI (`BOARD=BOARD_BUDDY`, `PRINTER_TYPE=2`). A feature flag is ON in tests only if its `set_feature_for_printers(...)` list in `ProjectOptions.cmake` contains `"UNITTESTS"`.

## Environment setup

In Claude Code on the web, `.claude/hooks/session-start.sh` has already done all of this, and `.venv/bin` is on `PATH`. Elsewhere, set it up to match CI (Ubuntu 24.04):

```bash
sudo apt-get install -y gettext           # msgfmt, needed by the translator tests
python3.12 -m venv .venv && .venv/bin/pip install -r requirements.txt   # or: python3.12 utils/bootstrap.py
source .venv/bin/activate
```

- **Use Python 3.12.** `utils/build_tests.py` uses PEP 701 f-strings, so 3.11 fails with `SyntaxError: f-string: expecting '}'`. `requirements.txt` pins `numpy==1.26.4`, which has no wheels for 3.13 or newer. It also imports `tree_sitter` / `tree_sitter_cpp` at the top of the script.
- **Build-time generators need these packages** from `requirements.txt`: `nunavut` (`nnvg`, for the Cyphal DSDL types), `pyyaml` (error codes), `polib`, `pillow` (fonts), `cbor2`, `ndeflib`, `simple_parsing` (OpenPrintTag).
- **How CMake finds Python:** it uses `<repo>/.venv`. Without one, activate another venv or export `BUDDY_NO_VIRTUALENV=1`.

## Build and run

```bash
python3 utils/build_tests.py --run -- -LE slow         # build all, run everything except [slow] (~700 tests, seconds)
python3 utils/build_tests.py <target> [<target>...]    # build only these targets, e.g. gcode_parser_tests
python3 utils/build_tests.py --list                    # list targets
python3 utils/build_tests.py -t -- -R "gcode_parser"   # run only, no rebuild
python3 utils/build_tests.py --debug <target>          # -Og for gdb
python3 utils/build_tests.py --coverage -- -R gcode    # HTML coverage in build/tests_coverage
```

Manual equivalent (the build dir is `build/tests`):

```bash
cmake -S . -B build/tests -G Ninja -DBOARD=BUDDY
ninja -C build/tests tests            # or a single target
ctest --test-dir build/tests -LE slow --output-on-failure -j"$(nproc)"
```

### Filtering gotchas

- **ctest test names are the Catch2 `TEST_CASE` names**, not the CMake target names. `-R` is a **case-sensitive** regex over those names. `-R cobs` matches nothing; `-R COBS` works.
- Catch2 tags become ctest labels: `-L translator`, `-LE slow`.
- To run one binary directly: `build/tests/tests/unit/<path>/<target> "[tag]"` or `... "Test case name"`.
- Tests that read data files must run from their own binary directory, e.g. `cd build/tests/tests/unit/common/gcode/reader && ./gcode_reader_test`. For gdb: `gdb -d <repo> ./<exe>`.

## Adding a test

1. Mirror the source path. `src/common/foo/bar.cpp` gets `tests/unit/common/foo/bar/` (or `tests/unit/common/foo/`), and `src/module/x` gets `tests/unit/module/x/`.
2. Name the test source differently from the unit under test, with a `.cpp` extension, e.g. `bar_tests.cpp`.
3. Create `CMakeLists.txt`. Compile the unit under test **directly** into the executable, or link its library if it is a `src/module` library:

   ```cmake
   add_executable(bar_tests ${CMAKE_SOURCE_DIR}/src/common/foo/bar.cpp bar_tests.cpp)
   target_include_directories(bar_tests PRIVATE . ${CMAKE_SOURCE_DIR}/src/common ${CMAKE_SOURCE_DIR}/tests/stubs)
   target_link_libraries(bar_tests logging-mock)      # optional: logging-mock, freertos-mock, SG14, magic_enum...
   add_catch_test(bar_tests)                          # links catch_main, registers with ctest, adds to `tests`
   ```

   For a `src/module` library: `add_executable(cobs_tests cobs_testing.cpp)`, `target_link_libraries(cobs_tests cobs SG14)`, `add_catch_test(cobs_tests)`.
4. Register the directory with `add_subdirectory(bar)` in the parent `CMakeLists.txt`.
5. Write the test:

   ```cpp
   #include <foo/bar.hpp>
   #include <catch2/catch_test_macros.hpp>

   TEST_CASE("Bar computes the thing", "[bar]") {
       SECTION("empty input") { REQUIRE(bar::compute({}) == 0); }
   }
   ```

   Tag slow tests (more than about a second) with `[slow]`.

### Stubbing heavy dependencies

- **Shared fakes** live in `tests/stubs/`: `timing.h`, `cmsis_os.h`, `dbg.h`, `i18n.h`, `config_store/store_instance.hpp`, Marlin bits, `device/board.h`, `global/option/*.h`. Mocks are in `tests/unit/mock/` (`logging-mock`, `freertos-mock`).
- **Per-test stubs:** put a `stub/` or `stub_include/` directory **first** in `target_include_directories` so it shadows the real header.
- **Asserts:** `bsod()` / `release_assert` fail the test through `tests_include/bsod.cpp`, which every test links. Assert that code aborts by structuring it so the failure path is testable. Never use libc `assert`.
- **Target-only code** (HAL, ISR, FreeRTOS internals): extract the pure logic into a function or class that the test can compile. That is the preferred refactor here.

## Before pushing

Run the full suite at least once, `python3 utils/build_tests.py --run`, exactly as CI does (`utils/holly/build-pr.jenkins`: `--run -- --output-on-failure`).
