# How to run unit tests?

## Prerequisites

- GCC (the host compiler; other compilers are not supported for unit tests)
- Python 3.12 with the packages from `requirements.txt`. Several build steps
  run Python generators (Cyphal DSDL via `nnvg`, fonts, error codes, OpenPrintTag
  test data). `utils/build_tests.py` itself needs at least 3.12, and the pinned
  `numpy==1.26.4` does not install on 3.13 or newer.
  `python3.12 utils/bootstrap.py` creates such a `.venv`, which CMake finds
  automatically. Activate it (`source .venv/bin/activate`) before running the
  commands below, or call `.venv/bin/python utils/build_tests.py` directly:
  unlike `utils/build.py`, `build_tests.py` does not switch to `.venv` by itself,
  and a system `python3` older than 3.12 cannot even start it.
- gettext (`msgfmt`) for the translator tests

## Quick Start (Recommended)

Use the automated build script for streamlined building and running tests:

```bash
# Once per shell: use the project's Python 3.12 virtual environment (see Prerequisites)
source .venv/bin/activate

# Build all tests (uses all CPU cores automatically)
python3 utils/build_tests.py

# Build and run all tests
python3 utils/build_tests.py --run

# Build and run, excluding slow tests
python3 utils/build_tests.py --run -- -LE slow

# Run tests only (skip build) - can be run from anywhere
python3 utils/build_tests.py --test
python3 utils/build_tests.py -t -- -LE slow           # Run fast tests only
python3 utils/build_tests.py -t -- -R gcode           # Run tests whose name contains "gcode"
```

### Build Options

```bash
# Build specific test targets only
python3 utils/build_tests.py cobs_tests ring_allocator_tests

# Build with debug symbols (for GDB debugging)
python3 utils/build_tests.py --debug

# Build with release flags (disables assertions via -DNDEBUG)
python3 utils/build_tests.py --release

# List available test targets
python3 utils/build_tests.py --list

# Clean rebuild
python3 utils/build_tests.py --rebuild

# Use custom number of parallel jobs
python3 utils/build_tests.py --jobs 8
```

### Build Types Explained

| Build Type | Flag | Optimization | Assertions | Use Case |
|------------|------|--------------|------------|----------|
| **Default** | (none) | `-Os` | Active | Recommended for development |
| **Debug** | `--debug` | `-Og` | Active | Debugging with GDB |
| **Release** | `--release` | `-Os` + `-O3` | Disabled (`-DNDEBUG`) | Production-like testing |

> **Note:** The default build type (no flag) keeps assertions active while using release-level optimization.
> This catches bugs via `assert()` while still being fast. Use `--release` only when you specifically
> need to test with assertions disabled.

### Running Tests with ctest Arguments

Use `--` to pass arguments directly to ctest:

```bash
# Build and run with ctest args
python3 utils/build_tests.py --run -- -R gcode         # Tests matching 'gcode'
python3 utils/build_tests.py --run -- -LE slow         # Exclude slow tests
python3 utils/build_tests.py --run -- --verbose        # Verbose output

# Run only (skip build) with ctest args
python3 utils/build_tests.py -t -- -R gcode -LE slow   # Combine filters
python3 utils/build_tests.py -t -- --rerun-failed      # Re-run failed tests
```

> The script automatically handles CMake/Ninja from bootstrap.py if not found on system.

> It is recommended to use GCC for compiling unit tests.

## Running Specific Tests (Recommended for Fast Iteration)

**You don't need to run all tests every time!** CTest provides powerful filtering.
Test names are the Catch2 `TEST_CASE` names (not the CMake target names), and `-R`
is a case-sensitive regular expression over them. Catch2 tags become ctest labels
for `-L`/`-LE`.

```bash
# Run tests by name pattern (regex)
ctest --test-dir build/tests -R gcode                  # Run all tests with "gcode" in the name
ctest --test-dir build/tests -R "gcode|json"           # Run gcode OR json tests
ctest --test-dir build/tests -R "^gcode_parser"        # Tests starting with "gcode_parser"

# Run tests by label/tag
ctest --test-dir build/tests -L translator             # Run only tests tagged with [translator]
ctest --test-dir build/tests -L "GcodeReader"          # Run only GcodeReader tests

# Exclude tests by label
ctest --test-dir build/tests -LE slow                  # Skip slow tests (recommended)

# Combine filters
ctest --test-dir build/tests -R gcode -LE slow         # Run gcode tests but skip slow ones
ctest --test-dir build/tests -L translator --verbose   # Run translator tests with verbose output
```

> Without `--test-dir`, ctest looks in the current directory and silently reports
> `Total Tests: 0` outside a test build directory.

**Common workflows:**
- **Daily development:** `python3 utils/build_tests.py -t -- -LE slow` (fast tests only, reduce execution time by ~90%)
- **Testing specific feature:** `python3 utils/build_tests.py -t -- -R <feature_name>`
- **Before committing:** `python3 utils/build_tests.py --run` (full suite)

## Code Coverage

Generate HTML coverage report:

```bash
# Build instrumented tests, run them, and open the HTML report
python3 utils/build_tests.py --coverage

# Pass ctest filters to run coverage on a subset of tests
python3 utils/build_tests.py --coverage -- -R gcode
```

Coverage builds use a separate build directory (`build/tests_coverage`) so they don't interfere with regular test builds.

## Manual Building (Alternative)

If you prefer to build manually or need more control:

```bash
# Configure (from the repository root; build/tests is the directory build_tests.py uses too)
cmake -S . -B build/tests -G Ninja -DBOARD=BUDDY

# Build all unit tests
ninja -C build/tests tests
```

> In case you don't have sufficient CMake or Ninja installed, you can use the ones downloaded by bootstrap.py:
> ```bash
> export PATH="$(python utils/bootstrap.py --print-dependency-directory cmake)/bin:$PATH"
> export PATH="$(python utils/bootstrap.py --print-dependency-directory ninja):$PATH"
> ```

### Running Tests Manually

```bash
# Using CTest
ctest --test-dir build/tests

# Using CMake directly
cmake --build build/tests --target test

# Using Ninja
ninja -C build/tests test
```

### Useful CTest Flags

- `--output-on-failure`: Show the output of failing tests (not enabled by default; CI uses it)
- `--verbose`: Always show all test output
- `--rerun-failed`: Re-run only tests that failed last time
- `-N`: List tests that would run without actually running them

Example:
```bash
ctest --test-dir build/tests -R gcode -LE slow --verbose   # Run gcode tests, skip slow, verbose output
ctest --test-dir build/tests -N                            # List all tests without running
ctest --test-dir build/tests -LE slow -N                   # List fast tests only
```

### Building with Debug Symbols

To enable debugging, build with the debug flag:

```bash
# Using the build script
python3 utils/build_tests.py --debug

# Or manually
cmake -S . -B build/tests -G Ninja -DBOARD=BUDDY -DCMAKE_BUILD_TYPE=Debug
```

### Debugging with GDB

You can debug tests using GDB, but the approach depends on whether the test has external dependencies:

#### For simple tests (no external dependencies):
Run GDB directly from the main project folder:
```bash
gdb ./build/tests/tests/unit/path/to/test_executable
```

#### For tests with external dependencies:
These tests must be run from their executable's directory to properly locate dependencies.

1. Navigate to the executable's directory:
```bash
cd build/tests/tests/unit/common/gcode/reader
```

2. Start GDB and specify the source directory and the test executable:
```bash
gdb -d <path_to_buddy> test_executable
```

> Tip: Run GDB with `-tui` flag for a nicer interface.

## How to create a new unit test?

1. Create a corresponding directory for it.
    - For example, for a unit in `src/guiapi/src/gui_timer.c` create directory `tests/unit/guiapi/gui_timer`.
2. Store your unittest cases within this directory together with their dependencies.
    Don't use the same file name for testing file and source file. Use '.cpp' extension.
3. Add a CMakeLists.txt with description on how to build your tests.
    - See other unit tests for examples, e.g. `tests/unit/common/ring_allocator/CMakeLists.txt`.
    - Register the executable with `add_catch_test(<target>)`, which links Catch2, adds it to the `tests` target and makes its test cases visible to ctest.
    - Don't forget to register any directory you add using `add_subdirectory` in CMakeLists.txt in the same directory.

## Tests on Windows

1. Download & install MinGW and make sure .../MinGW/bin/ is in your path.
2. Check if Python is installed.
3. Download & install some bash (GIT bash could be already installed).
4. Run bash and get to your repository directory (cd ...).
5. Run these to prepare for test:

```bash
rm -rf build/tests \
&& export PATH="$(python utils/bootstrap.py --print-dependency-directory cmake)/bin:$PATH" \
&& export PATH="$(python utils/bootstrap.py --print-dependency-directory ninja):$PATH" \
&& export CTEST_OUTPUT_ON_FAILURE=1 \
&& cmake -S . -B build/tests -G Ninja -DBOARD=BUDDY
```
