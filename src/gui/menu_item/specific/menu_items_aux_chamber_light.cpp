#include "menu_items_aux_chamber_light.hpp"

#include <common/marlin_vars.hpp>
#include <config_store/store_instance.hpp>
#include <gcode/inject_queue_actions.hpp>
#include <gui/menu_item/menu_item_utils.hpp>
#include <hwio_pindef.h>
#include <marlin_client.hpp>
#include <option/has_i2c_expander.h>

static_assert(HAS_I2C_EXPANDER(), "The aux chamber light is driven by the IO expander");

namespace {

constexpr uint8_t pin_mask = 1 << 0;

bool stored_state() {
    const bool is_output = !(config_store().io_expander_config_register.get() & pin_mask);
    return is_output && (config_store().io_expander_output_register.get() & pin_mask);
}

} // namespace

MI_AUX_CHAMBER_LIGHT::MI_AUX_CHAMBER_LIGHT()
    : WI_ICON_SWITCH_OFF_ON_t(stored_state(), _("Aux Chamber Light"), nullptr, is_enabled_t::yes,
        buddy::hw::io_expander2.is_initialized() ? is_hidden_t::no : is_hidden_t::yes) {}

void MI_AUX_CHAMBER_LIGHT::OnChange(size_t) {
    // The expander is polled for input pins from the marlin task, and its register reads are not
    // atomic on the bus, so the pin is driven from that task too.
    const bool sent = marlin_client::inject("M262 P0 B0")
        && (value() ? marlin_client::inject("M264 P0 B1") : marlin_client::inject("M264 P0 B0"));
    if (!sent) {
        set_value(stored_state());
    }
}

void MI_AUX_CHAMBER_LIGHT::Loop() {
    loop_gcode_inject_menu_item(*this, { .update_enabled = true, .update_icon = true });

    // Once the commands have run, show what the printer has, which a failed command or G-code may
    // have made different
    if (marlin_vars().inject_queue_empty) {
        set_value(stored_state());
    }
}
