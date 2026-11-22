/// @file
#pragma once

#include "WindowMenuItems.hpp"

/// Turns the calibration of the pressure advance before a print on and off
class MI_PRESSURE_ADVANCE_CALIBRATION : public WI_ICON_SWITCH_OFF_ON_t {
public:
    MI_PRESSURE_ADVANCE_CALIBRATION();
    void OnChange(size_t) final;
};
