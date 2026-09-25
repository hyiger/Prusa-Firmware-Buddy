---
name: add-gcode
description: Implement, modify or document a Prusa-specific G-code/M-code in Buddy firmware (src/marlin_stubs, PrusaGcodeSuite, GCodeParser2, gcode.cpp dispatch, overriding upstream Marlin handlers). Use when asked to add a new G-code, add a parameter to an existing one, change how a G-code behaves, or find where a G-code is implemented.
---

# Adding or changing a G-code

## Where G-codes live

- **Dispatch.** `lib/Marlin/Marlin/src/gcode/gcode.cpp` `process_parsed_command()` first calls `GcodeSuite::process_parsed_command_custom()` in `src/marlin_stubs/gcode.cpp` (enabled by the `PROCESS_CUSTOM_GCODE` define). That is a `switch` on letter, `codenum` and `subcode` that calls `PrusaGcodeSuite::Xnnn()`.
  - If it sets `processed = false`, Marlin's standard handler runs.
  - **Prusa handlers therefore take precedence** over Marlin's.
- **Prusa implementations:** `src/marlin_stubs/<Gnnn|Mnnn>.cpp`, some in subdirectories (`pause/`, `feature/`, `sdcard/`, …), with declarations in `src/marlin_stubs/PrusaGcodeSuite.hpp`.
- **Overrides of upstream Marlin G-codes** (M600–M604, M701/702, G27, M115, M486, M876, M20–M32, …) are done by defining `GcodeSuite::Mxxx()` in the stubs (e.g. `src/marlin_stubs/pause/M600.cpp`). The upstream implementation is removed from the fork or compiled out: Marlin sources are listed by hand in `lib/AddMarlin.cmake`, and Marlin's own `case` in `gcode.cpp` stays.
- **Finding an existing G-code:** `grep -rn "case <num>:" src/marlin_stubs/gcode.cpp`, then `grep -rn "void M<num>\|M<num>()" src/marlin_stubs lib/Marlin/Marlin/src/gcode`.

## Steps for a new G-code (example: M1234)

1. **Check the number is free.** Search the `gcode.cpp` switch and Marlin's `gcode.cpp`. Prusa internal and wizard codes use the M19xx and M99xx ranges. Prefer extending an existing G-code with a parameter over allocating a new number.
2. **Declare** it in `PrusaGcodeSuite.hpp`, inside `namespace PrusaGcodeSuite`, under the right `#if HAS_X()` (add the `#include <option/has_x.h>` there if it's missing):

   ```cpp
   #if HAS_MY_FEATURE()
   void M1234(); ///< Short description
   #endif
   ```

3. **Dispatch** it by adding a `case` to the `'M'` switch in `src/marlin_stubs/gcode.cpp`, in numeric order and with the same guard:

   ```cpp
   #if HAS_MY_FEATURE()
           case 1234:
               PrusaGcodeSuite::M1234();
               break;
   #endif
   ```

   For a subcode (`M1234.1`), nest `switch (parser.subcode)` as `M104.1` does, and set `processed = false` in `default:`.
4. **Implement** it in `src/marlin_stubs/M1234.cpp` with the new-style parser and the mandatory doc comment (format from `doc/contributing.md`):

   ```cpp
   #include "PrusaGcodeSuite.hpp"

   /** \addtogroup G-Codes
    * @{
    */

   /**
    *### M1234: Do the thing <a href="https://reprap.org/wiki/G-code#M1234:_...">M1234: ...</a>
    *
    *#### Usage
    *
    *    M1234 [ S | T | N ]
    *
    *#### Parameters
    *
    * - `S` - Speed in mm/s (1-500)
    * - `T` - Tool index (defaults to the active tool)
    * - `N` - Quoted name, e.g. `N"abc"`
    *
    *#### Examples
    *
    *    M1234 S100 ; do the thing at 100 mm/s
    */
   void PrusaGcodeSuite::M1234() {
       GCodeParser2 p;
       if (!p.parse_marlin_command()) {
           return; // parse errors are already reported to serial
       }

       uint16_t speed = 50;
       p.store_option_if_present('S', speed, uint16_t(1), uint16_t(500));   // range-checked; reports errors itself

       if (p.option<bool>('R').value_or(false)) { /* flag parameter */ }

       std::array<char, 32> name_buf;
       if (const auto name = p.option<std::string_view>('N', name_buf)) { /* *name */ }

       const auto tool = get_target_physical_from_command(p);   // or get_target_virtual_from_command(p) for MMU slots
       // ... call into a feature module; keep the G-code a thin adapter
   }

   /** @}*/
   ```

   - Use `GCodeParser2` (`src/common/gcode/gcode_parser.hpp`), not Marlin's global `parser`, in new code.
     - `option<T>(key, [min, max])` returns `std::optional<T>`.
     - `option_expected<T>` distinguishes a missing value from a parse error.
     - `option_multikey<T>({'A','B'})` tries several keys.
     - Enum and custom types work when a parser specialization exists; see `tests/unit/common/gcode/parser`.
   - Report user errors with `SERIAL_ERROR_MSG("...")` / `SERIAL_ECHOLNPGM(...)` and return. Don't `bsod` on bad input.
   - Anything long-running must not block Marlin's loop. Use `idle()`-based waits, `planner.synchronize()`, or an FSM (see the `fsm-dialog` skill) to interact with the user.
   - Put the logic in `src/feature/<name>/` (or `src/common/...`) and keep the G-code file a thin parser plus call. That makes the logic unit-testable.
5. **Build registration:** add the file to `src/marlin_stubs/CMakeLists.txt`, in the unconditional list or in an `if(HAS_MY_FEATURE)` block.
6. **G-code level and compatibility.**
   - Don't bump `GCODE_LEVEL`; slicer compatibility depends on it.
   - If slicers must detect the new capability, check how `M862.6` (feature checks, `src/marlin_stubs/M862_6.cpp`) and `M115` report features.
7. **Test.**
   - Parser behaviour: `tests/unit/common/gcode/parser` has examples of custom-type parsing.
   - Feature logic: unit tests next to the feature.
   - Build every preset where the guard is on, and one where it's off.

## Injecting G-codes from firmware code

- From the GUI or other tasks: `marlin_client::inject("M1234 S10")` (non-blocking, queued; takes a compile-time string or an `InjectQueueRecord`), `marlin_client::gcode("...")` (blocks until the server accepts it), or `marlin_client::gcode_printf(fmt, ...)`.
- From the Marlin task: `marlin_server::inject(GCodeLiteral("..."))` / `enqueue_gcode_printf(...)`, or call the implementation directly. `marlin_server::gcode_interrupt` is only for experts.
- G-code macro files and injection queues are in `src/common/gcode/inject_queue*`.
