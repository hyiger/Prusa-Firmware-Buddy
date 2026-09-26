#pragma once
#include <contactless_offset/tool_sensor.hpp>
#include "clo_config.hpp"
#include <cstdint>
#include <expected>

namespace tool_offset {

struct ToolOffset {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

/// Why a measurement failed, distinguishing causes that need a different remedy.
enum class MeasurementError : uint8_t {
    nozzle_temp_unavailable, ///< The hotend reports no nozzle temperature (on INDX: the tool is not heated, i.e. not picked)
    nozzle_too_hot, ///< The nozzle has not cooled down to the probing temperature limit
    homing_failed,
    sensor_probe_failed, ///< The loadcell touch on the sensor surface was not accepted
    sensor_not_detected, ///< The touch went past the expected sensor surface, the sensor is absent (dual-coil only)
    sensor_no_data, ///< No sweep produced sensor data that could be analyzed
    nozzle_not_found, ///< The sweeps ran, but did not locate the nozzle with enough confidence
    head_reset, ///< The toolhead board reset during the XY scan
};

struct MeasurementFailure {
    MeasurementError error;
    const char *message; ///< Detail for the log
};

// Measure the current tool's XYZ offset relative to the sensor reference position.
// Performs homing, Z probing, and XY scanning internally. The geometry in
// `config` selects single-coil (hunt X+Y over one coil) or dual-coil (XLS:
// X over one coil, Y over the other, each probed separately).
std::expected<ToolOffset, MeasurementFailure> measure_current_tool_offset(
    const ProbingConfig &config,
    const ToolOffset &actual_offset);

} // namespace tool_offset
