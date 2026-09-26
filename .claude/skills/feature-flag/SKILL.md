---
name: feature-flag
description: Add or change a compile-time feature option (HAS_* flags in ProjectOptions.cmake, generated option/has_*.h headers, define_boolean_option/define_enum_option) or gate code for specific printers/boards (PRINTER_IS_PRUSA_*(), BOARD_IS_*(), HAS_MINI_DISPLAY()). Use when a feature should exist only on some printers, when enabling an existing feature on another printer, or when fixing "#if HAS_X()" / missing-include preprocessor errors.
---

# Compile-time feature flags

## How it works

- **Definition.** `ProjectOptions.cmake` decides every flag from `PRINTER` and `BOARD`, which come from `utils/presets/presets.json`. The main tools:

  ```cmake
  set_feature_for_printers(HAS_MY_FEATURE "MK4" "COREONE" "COREONE_INDX" "COREONEL" "COREONEL_INDX")
  set_feature_for_printers_master_board(HAS_MY_FEATURE "XL" "COREONE")   # forced NO on puppy boards
  ```

  - Printer names: `MINI MK4 MK3.5 XL XL_DEV_KIT iX COREONE COREONE_INDX COREONEL COREONEL_INDX` (plus `NONE` for standalone puppies).
  - Add `"UNITTESTS"` to the list to turn the flag on in unit-test builds.
  - A `-DHAS_MY_FEATURE=NO` on the command line always wins. This is how the `xl-minimal` preset turns features off.
  - Derived flags such as `HAS_PUPPIES`, `HAS_GUI` and `HAS_PLANNER` are computed further down with plain `if()` plus `define_boolean_option(NAME ${VALUE})`.
- **Where a flag ends up.** Each flag becomes:
  - a **CMake variable**, for `if(HAS_MY_FEATURE) add_subdirectory(...)` / `target_sources(...)`;
  - a **generated header** `build/<cfg>/include/option/has_my_feature.h`, from `include/option/option_boolean.h.in`:

    ```c
    #define HAS_MY_FEATURE() 1
    namespace option { inline constexpr bool has_my_feature = 1; }
    ```

- **Enum and int options.** `define_enum_option(NAME X VALUE v ALL_VALUES "A;B;C")` and `define_int_option(...)` are in `cmake/Options.cmake`. They generate `X_IS_A()`, `X()` and `enum class` / `option::x` equivalents.

## Using flags in C++

```cpp
#include <option/has_my_feature.h>     // REQUIRED in every file that tests the flag

#if HAS_MY_FEATURE()                   // correct: function-like macro, always defined as 0 or 1
    #include <feature/my_feature/my_feature.hpp>
#endif

if constexpr (option::has_my_feature) { ... }   // preferred when both branches compile everywhere
```

- **Never** write `#ifdef HAS_MY_FEATURE` / `#if defined(...)`: the macro is always defined, so the check is always true.
  - Forgetting the include turns `#if HAS_MY_FEATURE()` into a preprocessor error ("missing binary operator"). That error is intentional.
  - Legacy `#ifdef` macros such as `HAS_ADC3` are unrelated and not generated options.
- Preprocessor directives are indented after the hash (`IndentPPDirectives: BeforeHash`); clang-format handles it.
- **Printer checks:**
  - `#include <printers.h>` gives `PRINTER_IS_PRUSA_MINI()`, `_MK4()`, `_MK3_5()`, `_XL()`, `_iX()`, `_COREONE()`, `_COREONEL()`, `_XL_DEV_KIT()`.
  - `_COREONE()` is also true for COREONE_INDX, `_COREONEL()` for COREONEL_INDX, and `_XL()` for XL_DEV_KIT.
  - MK4/MK4S/MK3.9/MK3.9S share one image and MK3.5/3.5S share another, so model variants inside one image are **runtime** checks (`PrinterModelInfo::current()`, `config_store().extended_printer_type`), not compile-time ones.
- **Board, MCU and display checks:**
  - `#include <device/board.h>` gives `BOARD_IS_BUDDY()`, `BOARD_IS_XBUDDY()`, `BOARD_IS_XLBUDDY()`, `BOARD_IS_DWARF()`, …
  - `<device/mcu.h>` gives `MCU_IS_STM32F4()` etc.
  - `#include <guiconfig/guiconfig.h>` gives `HAS_MINI_DISPLAY()` / `HAS_LARGE_DISPLAY()` / `HAS_ST7789_DISPLAY()` / `HAS_ILI9488_DISPLAY()`.
- **Prefer capability flags over printer checks.** Write `#if HAS_CHAMBER_API()`, not `#if PRINTER_IS_PRUSA_COREONE() || PRINTER_IS_PRUSA_XL()`. Enabling the feature on a new printer then becomes a one-line CMake change.
- **Sizes and tables.** When a flag changes an enum used in persisted data or in the FSM tables, keep every table in sync under the same guard; `static_assert`s usually catch mismatches. `ClientFSM` must stay below 32 entries.

## Adding a new flag: checklist

1. Add `set_feature_for_printers(HAS_X ...)` in `ProjectOptions.cmake`, next to related flags, with a short comment if the meaning isn't obvious. Use the `_master_board` variant if puppies must never see it.
2. Gate the sources in CMake: `if(HAS_X) add_subdirectory(x) endif()` in `src/feature/CMakeLists.txt` (or `target_sources` in the owning `CMakeLists.txt`). Gate Marlin sources in `lib/AddMarlin.cmake`.
3. Gate the code with `#include <option/has_x.h>` + `#if HAS_X()`. The places that are easy to miss:
   - the `PrusaGcodeSuite.hpp` declaration and the `gcode.cpp` case
   - `ClientFSM` / `client_response.hpp` / `DialogHandler` / `printer_state.cpp`
   - `config_store` items
   - menu item lists in `screen_menu_*.hpp`
4. In a feature-only `.cpp`, add `static_assert(HAS_X());` so it can't be compiled by accident.
5. Build one preset with the flag ON and one with it OFF (e.g. `xl-minimal`, or `-DHAS_X=NO`). The `-Werror` builds in CI catch unused variables and functions in the OFF branch.

## Enabling an existing feature on another printer

1. Add the printer to the `set_feature_for_printers` list.
2. Look for things that depend on the feature: Marlin `Configuration_<PRINTER>.h` defines, hardware pins or puppies, `config_store` defaults, per-printer menu lists, selftests, translations and images (`src/gui/res/<PRINTER>_used_imgs.txt`).
3. Build that printer's preset with `-Werror`. On MINI, check flash usage.
