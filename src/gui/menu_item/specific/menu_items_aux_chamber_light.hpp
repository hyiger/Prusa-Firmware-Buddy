/// @file
#pragma once

#include "WindowMenuItems.hpp"

/// Turns the LED strip on the GPIO board's first open-drain output (IO expander pin 0) on and off.
///
/// The state lives in the IO expander's output register, which the printer keeps across restarts.
/// `M264 P0` from G-code changes the same state, and the switch follows it.
/// The switch spins while its command waits for the printer, which during a print can take until
/// the current command ends.
/// Hidden when the GPIO board did not answer at startup.
class MI_AUX_CHAMBER_LIGHT : public WI_ICON_SWITCH_OFF_ON_t {
public:
    MI_AUX_CHAMBER_LIGHT();
    void OnChange(size_t) final;
    void Loop() final;
};
