/// @file
#include "PrusaGcodeSuite.hpp"

#include <feature/pressure_advance_calibration/pressure_advance_calibration.hpp>
#include <utils/string_builder.hpp>
#include <utils/variant_utils.hpp>

#include <limits>

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M574: Calibrate pressure advance
 *
 * Only Core One INDX
 *
 * Asks for the pressure advance of a tool to be calibrated with the load cell. The calibration itself
 * is done by G427 when it has the tool in the nozzle cleaner, about to purge it. The tool alternates
 * between a slow and a fast extrusion there and the firmware looks for the pressure advance with
 * which the force on the nozzle changes exactly when the flow does. `M573 R` then puts the result to use.
 *
 * Does nothing unless the calibration is turned on, see M573.
 *
 *#### Usage
 *
 *    M574 [ S | T | F | V | L | C | N | I ]
 *
 *#### Parameters
 *
 * - `S` - Tool to calibrate (defaults to the picked tool)
 * - `T` - Nozzle temperature to calibrate at [°C]. Required.
 * - `F` - Filament feedrate of the fast extrusion [mm/s], the one the pressure advance is to suit. Required.
 *         Unlike in a move, this is not mm/min. Limited to 1-8 mm/s.
 * - `V` - Accepted and ignored. Slicers send it and what it stands for has not been published.
 * - `L` - Filament feedrate of the slow extrusion [mm/s] (defaults to a quarter of `F`)
 * - `C` - How many times the extrusion goes fast with each pressure advance tried (2-8, defaults to 4)
 * - `N` - How many pressure advance values are tried (3-8, defaults to 4)
 * - `I` - Calibrate the picked tool right away instead of leaving it to G427, and report the result.
 *         Works with the calibration turned off, too. X and Y have to be homed.
 *
 *#### Examples
 *
 *    M574 S0 V35 T215 F1.6 ; calibrate tool 0 at 215 °C for 1.6 mm/s of filament once G427 gets to it
 *    M574 T215 F1.6 I      ; calibrate the picked tool right now
 */
void PrusaGcodeSuite::M574() {
    GCodeParser2 p;
    if (!p.parse_marlin_command()) {
        return;
    }

    using OptionError = GCodeParser2::OptionError;
    const auto invalid = [](const auto &result) {
        return !result && result.error() == OptionError::parse_error;
    };

    const auto immediately = p.option_expected<bool>('I');
    if (invalid(immediately)) {
        return;
    }
    const bool run_now = immediately.value_or(false);

    if (!run_now && !pressure_advance_calibration::is_enabled()) {
        return;
    }

    const auto temperature = p.option_expected<int16_t>('T', static_cast<int16_t>(EXTRUDE_MINTEMP), std::numeric_limits<int16_t>::max());
    const auto feedrate = p.option_expected<float>('F', 0.0f);
    if (!temperature || !feedrate) {
        if (!invalid(temperature) && !invalid(feedrate)) {
            SERIAL_ERROR_MSG("M574: T and F are required");
        }
        return;
    }

    pressure_advance_calibration::Request request {
        .nozzle_temperature = *temperature,
        .feedrate = *feedrate,
    };

    const auto slow_feedrate = p.option_expected<float>('L', 0.0f);
    if (invalid(slow_feedrate)
        || invalid(p.store_option_if_present('C', request.cycles))
        || invalid(p.store_option_if_present('N', request.steps))
        || invalid(p.option_expected<uint8_t>('S'))) {
        return;
    }
    if (slow_feedrate) {
        request.slow_feedrate = *slow_feedrate;
    }

    const auto tool = stdext::get_optional<PhysicalToolIndex>(GcodeSuite::get_target_physical_from_optional(p.option<uint8_t>('S'), true));
    if (!tool) {
        return;
    }

    if (!run_now) {
        pressure_advance_calibration::request(*tool, request);
        return;
    }

    if (tool != PhysicalToolIndex::currently_selected_opt()) {
        SERIAL_ERROR_MSG("M574: only the picked tool can be calibrated right away");
        return;
    }

    const auto result = pressure_advance_calibration::run_now(request);
    ArrayStringBuilder<64> sb;
    if (result) {
        sb.append_printf("Pressure advance T%u: %.4f", tool->to_raw(), static_cast<double>(*result));
        SERIAL_ECHO_START();
    } else {
        using Failure = pressure_advance_calibration::Failure;
        const char *reason = "";
        switch (result.error()) {
        case Failure::not_ready:
            reason = "printer not ready";
            break;
        case Failure::aborted:
            reason = "aborted";
            break;
        case Failure::measurement_failed:
            reason = "measurement failed";
            break;
        case Failure::no_result:
            reason = "no reliable result";
            break;
        }
        sb.append_printf("M574: %s", reason);
        SERIAL_ERROR_START();
    }
    SERIAL_ECHO(sb.str());
    SERIAL_EOL();
}

/** @}*/
