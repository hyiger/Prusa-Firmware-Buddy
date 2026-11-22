/// @file
#include "PrusaGcodeSuite.hpp"

#include <config_store/store_instance.hpp>
#include <feature/pressure_advance_calibration/pressure_advance_calibration.hpp>
#include <utils/string_builder.hpp>

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M573: Use or report calibrated pressure advance
 *
 * Only Core One INDX
 *
 * The pressure advance is calibrated per tool as asked for by M574. Slicers put `M573 R` after
 * the M572 of the filament, so that the calibrated value replaces the one of the filament
 * profile whenever there is one.
 *
 *#### Usage
 *
 *    M573 [ R | S ]
 *
 *#### Parameters
 *
 * - `R` - Set the pressure advance to the calibrated one of the picked tool. Does nothing if the tool
 *         has not been calibrated in this print or if the calibration is turned off.
 * - `S` - Turn the calibration off (`S0`) or on (`S1`). The setting is kept over restarts.
 *
 * Without parameters prints whether the calibration is on and the calibrated pressure advance of each tool.
 *
 *#### Examples
 *
 *    M572 S0.036 ; pressure advance from the filament profile
 *    M573 R      ; ... replaced by the calibrated one, if there is any
 */
void PrusaGcodeSuite::M573() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    const auto invalid = [](const auto &result) {
        return !result && result.error() == GCodeParser2::OptionError::parse_error;
    };

    const auto enable = p.option_expected<bool>('S');
    const auto restore = p.option_expected<bool>('R');
    if (invalid(enable) || invalid(restore)) {
        return;
    }

    if (enable) {
        config_store().pressure_advance_calibration.set(*enable);
    }

    if (restore.value_or(false)) {
        pressure_advance_calibration::apply();
    }

    if (enable || restore) {
        return;
    }

    SERIAL_ECHO_START();
    SERIAL_ECHOLNPAIR_F("Pressure advance calibration ", pressure_advance_calibration::is_enabled() ? "on" : "off");
    for (const auto tool : PhysicalToolIndex::all()) {
        if (const auto pressure_advance = pressure_advance_calibration::result(tool)) {
            ArrayStringBuilder<32> sb;
            sb.append_printf("T%u: %.4f", tool.to_raw(), static_cast<double>(*pressure_advance));
            SERIAL_ECHO_START();
            SERIAL_ECHO(sb.str());
            SERIAL_EOL();
        }
    }
}

/** @}*/
