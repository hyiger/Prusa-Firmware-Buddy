#pragma once

#include <screen_menu.hpp>
#include <window_menu_callback_item.hpp>

#include <MItem_menus.hpp>

#include <option/has_filament_slots.h>
#if HAS_FILAMENT_SLOTS()
    #include "screen_filament_library.hpp"
#endif

using ScreenFilamentManagement_ = ScreenMenu<EFooter::Off,
    MI_RETURN,
    MI_EDIT_FILAMENTS,
    MI_REORDER_FILAMENTS,
    MI_FILAMENTS_VISIBILITY
#if HAS_FILAMENT_SLOTS()
    ,
    MI_EDIT_FILAMENT_VENDORS,
    MI_FILAMENT_VENDORS_VISIBILITY,
    MI_EDIT_FILAMENT_COLORS,
    MI_FILAMENT_COLORS_VISIBILITY
#endif
    >;

class ScreenFilamentManagement final : public ScreenFilamentManagement_ {

public:
    ScreenFilamentManagement();
};
