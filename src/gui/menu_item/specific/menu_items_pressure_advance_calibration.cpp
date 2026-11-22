#include "menu_items_pressure_advance_calibration.hpp"

#include <config_store/store_instance.hpp>

MI_PRESSURE_ADVANCE_CALIBRATION::MI_PRESSURE_ADVANCE_CALIBRATION()
    : WI_ICON_SWITCH_OFF_ON_t(config_store().pressure_advance_calibration.get(), _("Auto Pressure Advance")) {}

void MI_PRESSURE_ADVANCE_CALIBRATION::OnChange(size_t) {
    config_store().pressure_advance_calibration.set(value());
}
