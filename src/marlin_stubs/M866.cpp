#include "PrusaGcodeSuite.hpp"

#include <array>

#include <filament.hpp>
#include <filament_library.hpp>
#include <tool_index.hpp>

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M866: Filament slot vendor and color
 *
 * Sets the vendor and color of the filament loaded in a tool, as the Filament slots screen does,
 * then prints the tool's filament type, vendor and color.
 * The tool must have filament loaded. Vendor and color are cleared when it is unloaded.
 *
 *#### Usage
 *
 *    M866 T<tool> [ V"<vendor>" ] [ C"<color>" ]
 *
 *#### Parameters
 *
 * - `T` - Tool, indexed from 0
 * - `V` - Vendor name, as listed in the vendor library (case insensitive). "" clears the vendor.
 * - `C` - Color name, as listed in the color library (case insensitive). "" clears the color.
 *
 *#### Examples
 *
 *    M866 T0 V"3D-Fuel" C"Red" ; Tool 1 holds 3D-Fuel filament in red
 *    M866 T0                   ; Print what tool 1 holds
 *
 */
void PrusaGcodeSuite::M866() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    const auto tool_index = p.option<uint8_t, uint8_t, uint8_t>('T', 0, VirtualToolIndex::count - 1);
    if (!tool_index) {
        SERIAL_ERROR_MSG("Tool not specified");
        return;
    }
    const VirtualToolIndex tool = VirtualToolIndex::from_raw(*tool_index);

    const FilamentType type = FilamentType::for_tool(tool);
    if (type == FilamentType::none) {
        SERIAL_ERROR_MSG("No filament loaded in the tool");
        return;
    }

    FilamentSpool spool = FilamentSpool::for_tool(tool);

    std::array<char, filament_library_name_buffer_size> vendor_buffer;
    if (const auto vendor_name = p.option<std::string_view>('V', vendor_buffer)) {
        spool.vendor = FilamentVendor::from_name(*vendor_name);
        if (!vendor_name->empty() && !spool.vendor) {
            p.report_option_error('V', "Unknown vendor");
            return;
        }
    }

    std::array<char, filament_library_name_buffer_size> color_buffer;
    if (const auto color_name = p.option<std::string_view>('C', color_buffer)) {
        spool.color = FilamentColor::from_name(*color_name);
        if (!color_name->empty() && !spool.color) {
            p.report_option_error('C', "Unknown color");
            return;
        }
    }

    FilamentSpool::set_for_tool(tool, spool);

    const auto material = type.parameters().name;
    const auto vendor = spool.vendor.name();
    const auto color = spool.color.name();
    const auto rgb = spool.color.color();
    const auto hex = rgb ? color_to_hex(*rgb) : ColorHexString {};
    SERIAL_ECHOLNPAIR("material:", material.data());
    SERIAL_ECHOLNPAIR("vendor:", vendor.data());
    SERIAL_ECHOLNPAIR("color:", color.data());
    SERIAL_ECHOLNPAIR("color_hex:", hex.data());
}

/** @}*/
