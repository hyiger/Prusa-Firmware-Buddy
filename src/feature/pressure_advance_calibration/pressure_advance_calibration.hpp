/// @file
/// @brief Calibration of the pressure advance with the load cell
///
/// The tool extrudes into the wastebin while the load cell tells how the nozzle pressure follows
/// the extruder. See pressure_advance_estimator.hpp for how the pressure advance comes out of that.
///
/// Except for process_sample(), everything is to be called from the marlin task only.
#pragma once

#include <cstdint>
#include <expected>
#include <optional>

#include <tool_index.hpp>

namespace pressure_advance_calibration {

/// What to calibrate the pressure advance for
struct Request {
    /// [°C]
    int16_t nozzle_temperature;

    /// [mm/s of filament] Feedrate of the flow the pressure advance is to suit
    float feedrate;

    /// [mm/s of filament] Feedrate the extrusion drops to in between, a quarter of `feedrate` if not given
    std::optional<float> slow_feedrate = std::nullopt;

    /// How many times the extrusion goes fast with each pressure advance tried
    uint8_t cycles = 4;

    /// How many pressure advance values are tried
    uint8_t steps = 4;
};

enum class Failure : uint8_t {
    /// The tool is not the picked one, or the nozzle cannot get where it needs to be
    not_ready,

    /// The print got stopped in the meantime
    aborted,

    /// The force does not tell enough about the nozzle pressure
    measurement_failed,

    /// The pressure advance does not come out of the measurement reliably, or not within the supported range
    no_result,
};

/// @returns whether the user has the calibration turned on. Nothing gets calibrated or applied otherwise.
bool is_enabled();

/// Has the pressure advance of the tool calibrated the next time run_requested() is called for it
void request(PhysicalToolIndex tool, const Request &request);

/// Calibrates the pressure advance of the tool, if it has been requested.
/// The tool has to be picked, with its nozzle at the purge position of the nozzle cleaner.
/// Leaves the nozzle hot, with a blob on it.
void run_requested(PhysicalToolIndex tool);

/// Calibrates the pressure advance of the picked tool right away.
/// Takes the nozzle to the nozzle cleaner and leaves it there, purged and cleaned.
/// @returns the calibrated pressure advance [s]
std::expected<float, Failure> run_now(const Request &request);

/// @returns the calibrated pressure advance of the tool [s], if there is one
std::optional<float> result(PhysicalToolIndex tool);

/// Sets the pressure advance to the calibrated one of the picked tool.
/// @returns whether there was one to set
bool apply();

/// Forgets all the requests and calibrated values
void reset();

/// Takes a load cell reading with the extruder position at the same instant.
/// To be called from wherever the load cell is read, always from the same context.
void process_sample(uint32_t time_us, float force, float e_position);

} // namespace pressure_advance_calibration
