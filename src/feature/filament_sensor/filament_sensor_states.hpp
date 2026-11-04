/// @file
#pragma once
#include <stdint.h>
#include <optional>

class IFSensor;

enum class FilamentSensorState : uint8_t {
    NotInitialized, // enable enters this state too
    NotCalibrated,
    HasFilament,
    NoFilament,
    NotConnected,
    Disabled,
};

struct FilamentSensorStateAndValue {
    FilamentSensorState state = FilamentSensorState::NotInitialized;
    int32_t value = 0;

    bool operator==(const FilamentSensorStateAndValue &) const = default;
};

/// Returns whether the passed FilamentSensorState means that the filament sensor is working (functional, set up and such)
constexpr inline bool is_fsensor_working_state(FilamentSensorState state) {
    return state == FilamentSensorState::NoFilament || state == FilamentSensorState::HasFilament;
}

constexpr inline bool is_fsensor_working_or_ncal_state(FilamentSensorState state) {
    return is_fsensor_working_state(state) || state == FilamentSensorState::NotCalibrated;
}

bool is_fsensor_working_state(IFSensor *sensor);

/// \returns whether filament surely remains in a filament path, given the states of its extruder
/// and side sensors (nullopt for a sensor the path doesn't have)
/// The extruder sensor decides when the path has one, otherwise the side sensor does.
/// A sensor that is not working (disabled, not calibrated, disconnected and such) cannot report
/// filament, so it never holds the user back.
constexpr inline bool has_filament_surely_in_path(std::optional<FilamentSensorState> extruder, std::optional<FilamentSensorState> side) {
    return (extruder ? extruder : side) == FilamentSensorState::HasFilament;
}
