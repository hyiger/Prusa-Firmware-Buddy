/// @file
#pragma once

#include "MItem_menus.hpp"
#include "MItem_tools.hpp"
#include <basic_screen_menu.hpp>
#include <option/has_e2ee_support.h>
#include <option/has_internal_storage_flash.h>
#include <option/has_usb_drive_format.h>

using ScreenMenuSystemBase = BasicScreenMenu<
    MI_SAVE_DUMP,
    MI_LOG_TO_TXT,
    MI_DEVHASH_IN_QR,
    MI_LOAD_SETTINGS,
#if HAS_E2EE_SUPPORT()
    MI_E2EE,
#endif
#if HAS_INTERNAL_STORAGE_FLASH()
    MI_INTERNAL_STORAGE,
#endif
#if HAS_USB_DRIVE_FORMAT()
    MI_FORMAT_USB_DRIVE,
#endif
    MI_OPEN_FACTORY_RESET>;

class ScreenMenuSystem final : public ScreenMenuSystemBase {
public:
    ScreenMenuSystem();
};
