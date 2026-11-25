/// @file
/// @brief Calibration of the pressure advance with the load cell
///
/// The tool extrudes into the wastebin while the load cell tells how the nozzle pressure follows
/// the extruder. See pressure_advance_estimator.hpp for how the pressure advance comes out of that.
///
/// Nothing may touch the nozzle meanwhile, which the silicone V-groove of the nozzle cleaner does at
/// the purge position. So the measurement is done further along the lane the cleaner sequences move
/// the nozzle in. The head never leaves that lane, so that the sequences and whatever takes the head
/// out of the cleaner afterwards find it where they expect it.
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

    /// [mm/s of filament] Feedrate of the fastest flow of the print, which the fast extrusion is done at.
    /// A nozzle needs the less pressure advance the faster the filament flows, so what is measured only
    /// suits prints that go about this fast.
    float feedrate;

    /// [mm/s of filament] Feedrate the extrusion drops to in between, a tenth of the fast one, but 0.2 at the least, if not given
    std::optional<float> slow_feedrate = std::nullopt;

    /// How many times the extrusion goes fast with each pressure advance tried.
    /// As many as a moderate length of filament allows if not given: more of them the slower the flow,
    /// whose force takes more of them to average out.
    std::optional<uint8_t> cycles = std::nullopt;

    /// How many pressure advance values are tried
    uint8_t steps = 4;

    /// Where on the lane of the nozzle cleaner to measure, as the Y of a G750 move.
    /// A spot known to be clear of the silicone if not given.
    std::optional<float> cleaner_y = std::nullopt;

    /// Whether to log how the force follows the flow with each pressure advance tried
    bool log_response_curves = false;

    /// Pressure advance range [s] to try `steps` evenly spread values of, instead of closing in on
    /// the value the calibration ends up with. For looking into how the force responds to the others.
    struct Scan {
        float first;
        float last;
    };
    std::optional<Scan> scan = std::nullopt;

    /// The largest pressure advance the calibration can try or end up with
    static constexpr float max_pressure_advance = 0.2f;

    /// The part of the lane the eject strokes of the nozzle cleaner go through, so the nozzle can go anywhere in it
    static constexpr float min_cleaner_y = 76;
    static constexpr float max_cleaner_y = 93;
};

enum class Failure : uint8_t {
    /// The tool is not the picked one, or the nozzle cannot get where it needs to be
    not_ready,

    /// The print got stopped in the meantime
    aborted,

    /// Nothing comes from the load cell
    no_samples,

    /// Too many load cell samples got lost on the way
    samples_lost,

    /// The extruder did not move the way the measurement needs it to
    extruder_off_pattern,

    /// The force changes too little with the flow, or not in step with it
    weak_force,

    /// The force does not follow the pressure advance the way the nozzle pressure would
    inconsistent,

    /// The measurements with the individual pressure advance values do not agree with each other
    noisy,

    /// The pressure advance comes out outside of the supported range
    out_of_range,

    _cnt,
};

/// @returns what went wrong, in a few words of English that have a translation
const char *to_string(Failure failure);

/// @returns whether the user has the calibration turned on. Nothing gets calibrated or applied otherwise.
bool is_enabled();

/// Has the pressure advance of the tool calibrated the next time run_requested() is called for it
void request(PhysicalToolIndex tool, const Request &request);

/// Calibrates the pressure advance of the tool, if it has been requested.
/// The tool has to be picked, with its nozzle at the purge position of the nozzle cleaner.
/// Leaves the nozzle there again, hot and with a blob on it.
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
