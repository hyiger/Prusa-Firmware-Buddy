---
name: fsm-dialog
description: Implement Marlin-side flows that show a dialog, wizard or calibration screen on the printer and wait for the user (ClientFSM, Phases*/PhaseResponses, Response, marlin_server::FSM_Holder, wait_for_response, fsm_change, DialogHandler, ScreenFSM, FrameDefinition). Use when a G-code/selftest/calibration/print-time event needs user interaction or progress UI, when adding a phase or button to an existing wizard, or when adding a new Response button type.
---

# FSM dialogs: Marlin-side UI flows

Code running in the **Marlin task** (G-codes, selftests, pause/load, calibrations) must never touch GUI objects. It publishes an **FSM state** (which FSM, which phase, plus 4 bytes of data). The GUI task (`DialogHandler`) renders it; user button presses come back as a `Response`. The same state is exposed to Prusa Connect and PrusaLink (`src/state/printer_state.cpp`).

Reference implementation to copy from, small and modern: **door sensor calibration** (`src/feature/door_sensor_calibration/`, `PhaseDoorSensorCalibration`, `ClientFSM::DoorSensorCalibration`).

## Server side (Marlin task)

```cpp
#include <marlin_server.hpp>
#include <client_response.hpp>

void run_my_wizard() {
    marlin_server::FSM_Holder holder { PhaseMyWizard::intro };   // creates the FSM; destroys it on scope exit (nestable)

    switch (marlin_server::wait_for_response(PhaseMyWizard::intro)) {   // blocks via idle(), Marlin keeps running
    case Response::Continue: break;
    case Response::Abort: return;
    default: bsod_unreachable();
    }

    marlin_server::fsm_change(PhaseMyWizard::working, fsm::PhaseData { progress_pct, 0, 0, 0 });  // progress/data for the GUI
    do_work();                                    // long work must call idle() regularly
    marlin_server::fsm_change(PhaseMyWizard::done);
    marlin_server::wait_for_response(PhaseMyWizard::done);
}
```

- **Polling:** `get_response_from_phase(phase)` polls without blocking, for loops that do work while waiting. `wait_for_response(phase, timeout_ms)` returns `Response::_none` on timeout.
- **Larger payloads:** use `fsm_change_extended(...)` / `FSMExtendedDataManager` (`marlin_server_extended_fsm_data.hpp`). Typed responses use `wait_for_response_variant` / `FSMResponseVariant`.
- **Complex flows:** `FSMHandler` (`src/common/fsm_handler.hpp`) provides per-phase init, loop and exit callbacks. Otherwise the door sensor pattern works well: a class with `curr_phase` and `run_current_phase()`.
- **Parameterised flows:** a G-code usually starts the flow (`M1980` → `door_sensor_calibration::run(args)`); see the `add-gcode` skill.

## Adding a phase or button to an existing FSM

1. Add the enum value to its `Phases*` / `Phase*` enum. It lives in `src/common/marlin_server_types/client_response.hpp`, or for newer FSMs in `src/common/marlin_server_types/fsm/<name>_phases.hpp`. Keep `_last` / `_cnt` correct.
2. Add a row to its responses table: an `EnumArray<Phase, PhaseResponses, CountPhases<Phase>()>` with up to 4 buttons, e.g. `{ Phase::x, { Response::Continue, Response::Abort } }`. The first listed button is the default focus. A `static_assert` checks that every phase has a row.
3. Add a frame for the phase in the GUI screen's `FrameDefinitionList` (see below).
4. Handle the phase and its responses on the server side.

## Adding a new FSM: every place to touch

| # | File | What |
|---|---|---|
| 1 | `src/common/marlin_server_types/client_fsm_types.hpp` | add `ClientFSM::MyWizard` under `#if HAS_X()`. **Must stay below 32 entries** (`static_assert`), because only 5 bits are serialized. Add the option include. |
| 2 | `client_response.hpp` (or new `fsm/my_wizard_phases.hpp`) | `enum class PhaseMyWizard : PhaseUnderlyingType { ..., _last = ... };`, `constexpr inline ClientFSM client_fsm_from_phase(PhaseMyWizard) { return ClientFSM::MyWizard; }`, and the `my_wizard_responses` `EnumArray` |
| 3 | `src/common/marlin_server_types/client_response.cpp` | add `{ ClientFSM::MyWizard, my_wizard_responses }` to the `fsm_phase_responses` master table |
| 4 | `src/common/fsm_states.cpp` | give it a priority in the `score()` switch: 1 for full-screen wizards and selftests, higher for dialogs that must cover them |
| 5 | `src/gui/dialogs/DialogHandler.cpp` | add `FSMScreenDef<ClientFSM::MyWizard, ScreenMyWizard>` (full screen) or `FSMDialogDef<ClientFSM::MyWizard, DialogMyWizard>` (overlay, static buffer of about 1.3 kB) to `FSMDisplayConfig`. A `static_assert` requires exactly one entry per `ClientFSM`. |
| 6 | `src/state/printer_state.cpp` | add the case to the `get_state()` switch (usually `DeviceState::Busy`) and to the `get_state_with_dialog()` switch. This decides what Connect and PrusaLink show and whether remote actions are allowed. `-Werror=switch` breaks the build if a case is missing. |
| 7 | `src/common/marlin_server_types/python_binding/fsm_python_binding.cpp` | *optional*: `export_enum<PhaseMyWizard>(m, "PhaseMyWizard")` if integration or test tooling needs it |
| 8 | GUI screen (e.g. `src/feature/my_wizard/screen_my_wizard.{hpp,cpp}`) | see below; add it to CMake under `if(HAS_X)` |

### GUI screen for a wizard

```cpp
class ScreenMyWizard final : public ScreenFSM {
public:
    ScreenMyWizard() : ScreenFSM { N_("MY WIZARD"), GuiDefaults::RectScreenNoHeader } {
        header.SetIcon(&img::selftest_16x16);
        CaptureNormalWindow(inner_frame);
        create_frame();
    }
    ~ScreenMyWizard() { destroy_frame(); }
    PhaseMyWizard get_phase() const { return GetEnumFromPhaseIndex<PhaseMyWizard>(fsm_base_data.GetPhase()); }
protected:
    void create_frame() final { Frames::create_frame(frame_storage, get_phase(), &inner_frame); }
    void destroy_frame() final { Frames::destroy_frame(frame_storage, get_phase()); }
    void update_frame() final { Frames::update_frame(frame_storage, get_phase(), fsm_base_data.GetData()); }
};

// in the .cpp, anonymous namespace:
constexpr auto txt_intro = N_("Explain what will happen.");
using Frames = FrameDefinitionList<ScreenMyWizard::FrameStorage,
    FrameDefinition<PhaseMyWizard::intro, FrameTextPrompt, PhaseMyWizard::intro, txt_intro>,
    FrameDefinition<PhaseMyWizard::working, FrameMyWorking>,   // custom frame: ctor(window_frame_t *parent) + update(fsm::PhaseData)
    FrameDefinition<PhaseMyWizard::done, FrameTextPrompt, PhaseMyWizard::done, txt_done>>;
```

- `FrameDefinition<Phase, Frame, Args...>` forwards `Args...` to the frame's constructor, after `parent`.
- Standard frames live in `src/gui/standard_frame/`: `FrameTextPrompt`, `FramePrompt`, `FrameQRPrompt`, `FrameProgressPrompt`, `FrameWait`, `FrameCalibrationTextWithImage`, and others. Check each constructor for its parameters.
- To send a small struct as phase data, use `fsm::serialize_data(my_struct)` on the server and `fsm::deserialize_data<T>(data)` in the frame. The struct must be trivially copyable and at most 4 bytes.
- Buttons are drawn from the phase's response table (`RadioButtonFSM`), so you never hand-code button handling. Presses reach the server via `marlin_client::FSM_response(phase, response)`.
- Custom frames take `window_frame_t *parent` and optionally implement `update(fsm::PhaseData)`.

## Adding a new `Response` (button)

- `src/common/marlin_server_types/general_response.hpp`: add it to `enum class Response`, keeping the documented ordering rules.
- `src/common/general_response.cpp`: add `R(Name)`, the string exposed to Connect and Link.
- `src/common/client_response_texts.hpp`: add the button label, `N_("LABEL")`.

## Verify

- Build every preset where the flag is on, plus one where it is off. The switches in `printer_state.cpp`, `fsm_states.cpp` and `DialogHandler` are the usual failures.
- The FSM tables have `static_assert`s; read the error text, which usually names the missing phase or FSM.
- MINI renders frames at 240×320. Check that text fits, or use the `resolution_240x320/` variants where they exist.
