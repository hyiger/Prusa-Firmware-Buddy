---
name: gui-menu
description: Add or change on-printer UI in Buddy firmware - menu items (MI_* classes, WiSpin, WI_ICON_SWITCH_OFF_ON_t, MenuItemSwitch, MI_SCREEN submenus), menu screens (ScreenMenu/BasicScreenMenu), message boxes (MsgBoxQuestion etc.), translated strings and icons, for both MINI (240x320) and large-display (480x320) printers. Use for "add a setting to the menu", "add a submenu", "ask the user a yes/no question from the GUI", "show a value in a menu". For dialogs driven from Marlin-side code use the fsm-dialog skill instead.
---

# GUI menus, screens and message boxes

- **Where the code lives:** `src/guiapi/` is the widget toolkit; `src/gui/` holds the app's screens and menu items.
- **Threading:** the GUI runs in the `display` task. It **must not** call Marlin directly. Read state through `marlin_vars()`, and act through `marlin_client::` (`gcode()`, `inject()`, `FSM_response()`, …) or `config_store()`.

## Anatomy

- **Menu screen:** `BasicScreenMenu<MI_A, MI_B, ...>` (`src/gui/basic_screen_menu.hpp`) prepends a Return item and uses the default footer. `ScreenMenu<EFooter::On|Off, MI_RETURN, ...>` gives full control. Items are **member objects**, constructed in place; reach one with `Item<MI_A>()`.
- **Menu items** are named `MI_UPPER_SNAKE_CASE` and declared in `src/gui/MItem_<area>.hpp`, with the implementation in the matching `.cpp`. Newer generic items live in `src/gui/menu_item/`. The base classes:
  - `IWindowMenuItem`: plain item. Override `click(IWindowMenu&)` and optionally `Loop()`.
  - `WI_ICON_SWITCH_OFF_ON_t`: on/off switch. Override `OnChange(size_t old_index)`.
  - `MenuItemSwitch` / `MenuItemSelectMenu`: pick from a list.
  - `MenuItemToggleSwitch`: tristate.
  - `WiSpin`: numeric value, configured with a `NumericInputConfig`. Override `OnClick()` and read `value()`.
  - `WiInfo<N>`: read-only value.
  - `MenuItemGcodeAction`: runs a G-code.
- **Submenu entry:** `using MI_FOO = MI_SCREEN<N_("Foo"), class ScreenMenuFoo>;` in `src/gui/MItem_menus.hpp`, plus `template struct MI_SCREEN_CTOR<ScreenMenuFoo>;` and an `#include "screen_menu_foo.hpp"` in `src/gui/MItem_menus.cpp`.
- **Screen lifetime:** only one screen object exists at a time, built into a static buffer by `ScreenFactory`. Navigation uses `Screens::Access()->Open(ScreenFactory::Screen<ScreenMenuFoo>)`, `Close()` and `CloseAll()`. A `static_assert` fires if a screen outgrows the buffer; keep screens lean.

## Recipe: add a settings item backed by `config_store`

1. Add the persistent item first if it's new (see the `config-store` skill).
2. Declare the item in the right `MItem_*.hpp`:

   ```cpp
   class MI_MY_FEATURE : public WI_ICON_SWITCH_OFF_ON_t {
       static constexpr const char *const label = N_("My Feature");
   public:
       MI_MY_FEATURE();
   protected:
       void OnChange(size_t old_index) override;
   };

   class MI_MY_TIMEOUT : public WiSpin {
       static constexpr const char *const label = N_("My Timeout");
   public:
       MI_MY_TIMEOUT();
   protected:
       void OnClick() override;
   };
   ```

3. Implement it in the `.cpp`:

   ```cpp
   MI_MY_FEATURE::MI_MY_FEATURE()
       : WI_ICON_SWITCH_OFF_ON_t(config_store().my_feature_enabled.get(), _(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {}
   void MI_MY_FEATURE::OnChange(size_t old_index) {
       config_store().my_feature_enabled.set(!old_index);
   }

   static constexpr NumericInputConfig my_timeout_config {
       .min_value = 0, .max_value = 600, .step = 10, .unit = Unit::second,
   };
   MI_MY_TIMEOUT::MI_MY_TIMEOUT()
       : WiSpin(config_store().my_timeout_s.get(), my_timeout_config, _(label), nullptr, is_enabled_t::yes, is_hidden_t::no) {}
   void MI_MY_TIMEOUT::OnClick() {
       config_store().my_timeout_s.set(value());
   }
   ```

4. Add the item to the screen's type list, e.g. `src/gui/screen_menu_user_interface.hpp`, guarded by the same `#if HAS_X()` as the feature. Many screens have per-printer lists (`#if PRINTER_IS_PRUSA_MINI()` …); add it to each list where it applies.
5. If the setting affects behaviour that is already running, notify the owner in `OnChange`/`OnClick`, e.g. with a `marlin_client::` call. Don't write Marlin state directly.

## Recipe: add a new menu screen

```cpp
// src/gui/screen_menu_foo.hpp
#pragma once
#include "MItem_tools.hpp"
#include <basic_screen_menu.hpp>

using ScreenMenuFoo__ = BasicScreenMenu<MI_MY_FEATURE, MI_MY_TIMEOUT>;
class ScreenMenuFoo final : public ScreenMenuFoo__ {
public:
    ScreenMenuFoo();
};

// src/gui/screen_menu_foo.cpp
#include "screen_menu_foo.hpp"
#include <img_resources.hpp>
ScreenMenuFoo::ScreenMenuFoo()
    : ScreenMenuFoo__ { _("FOO"), &img::settings_16x16 } {}
```

- **Register it:** add the `.cpp` to `src/gui/CMakeLists.txt` (inside `if(HAS_X)` when gated), then add the `MI_SCREEN` alias and `MI_SCREEN_CTOR` instantiation as above. Put `MI_FOO` into the parent menu's item list.
- **Periodic refresh** (live values): override `windowEvent(window_t *sender, GUI_event_t event, void *param)`, handle `GUI_event_t::LOOP`, then call the base. `screen_menu_lang_and_time.cpp` is an example.
- **Enabling and hiding items at runtime:** `Item<MI_X>().set_enabled(bool)` / `.set_is_hidden(bool)`.

## Message boxes (GUI-side questions)

```cpp
#include <window_msgbox.hpp>
if (MsgBoxQuestion(_("Really reset all settings?"), Responses_YesNo) == Response::Yes) { ... }
MsgBoxInfo(_("Done."), Responses_Ok);
MsgBoxWarning(_("Nozzle is hot."), Responses_Ok);
```

- These run a nested GUI loop and return the chosen `Response`.
- Only call them from GUI code, never from the Marlin task: server-side flows use an FSM.
- Button sets are `Responses_*` in `src/guiapi/include/window_msgbox.hpp`. A new `Response` value also needs its text in `src/common/client_response_texts.hpp`.

## Text, fonts, icons

- **Every user-visible string** goes through `_()` (translated now) or `N_()` (marked; translate later with `_()`).
  - Keep strings short: MINI's display is 240 px wide, and German or Polish translations run longer.
  - Changing the English text invalidates the existing translations. Don't edit `src/lang/po/*`; translators update them.
- **Formatted text:** use `StringBuilder` / `ArrayStringBuilder<N>`, or copy a translated format to RAM with `_(fmt).copyToRAM(buf, n)` and then `snprintf`. Don't `snprintf` into a `string_view_utf8`.
- **Icons:**
  - Add `src/gui/res/png/<name>_<W>x<H>.png` and list it in `src/gui/res/<PRINTER>_used_imgs.txt` for each printer that uses it; unlisted icons are not packed.
  - Use it as `&img::<name>_<W>x<H>`.
  - Signature Oak builds overlay `png_brass/`. A new icon with Prusa-orange pixels needs a brass copy there, or an entry in `NO_BRASS_REQUIRED` in `utils/generate-icon-parity-report.py`. CI's report lists the missing ones but doesn't fail the build.
  - An icon missing from a printer's `_used_imgs.txt` compiles but fails to link (`undefined reference to img::...`).
- **Display differences:** MINI (`HAS_MINI_DISPLAY()`, ST7789, footer inside menus) vs large (`HAS_LARGE_DISPLAY()`, ILI9488). Geometry lives in `include/guiconfig/GuiDefaults.hpp`. Some layouts have separate `resolution_240x320/` and `resolution_480x320/` dialog variants.

## Verify

- Build `mini` and one large-display preset (`mk4` or `coreone`). The UI code differs the most between them.
- `tests/unit/gui/` covers some window, layout and text-fit logic. Translated error texts are checked for fit by `tests/unit/lang/text_fit`.
