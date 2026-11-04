#include <feature/filament_sensor/filament_sensor_states.hpp>

#include <catch2/catch_test_macros.hpp>

using State = FilamentSensorState;

TEST_CASE("has_filament_surely_in_path: extruder sensor decides when present", "[fsensor]") {
    CHECK(has_filament_surely_in_path(State::HasFilament, State::NoFilament));
    CHECK(has_filament_surely_in_path(State::HasFilament, std::nullopt));
    CHECK_FALSE(has_filament_surely_in_path(State::NoFilament, State::HasFilament));
    // A non-working extruder sensor does not fall back to the side sensor
    CHECK_FALSE(has_filament_surely_in_path(State::Disabled, State::HasFilament));
}

TEST_CASE("has_filament_surely_in_path: side sensor decides without extruder sensor", "[fsensor]") {
    // INDX tools have side sensors only
    CHECK(has_filament_surely_in_path(std::nullopt, State::HasFilament));
    CHECK_FALSE(has_filament_surely_in_path(std::nullopt, State::NoFilament));
    CHECK_FALSE(has_filament_surely_in_path(std::nullopt, std::nullopt));
}

TEST_CASE("has_filament_surely_in_path: non-working sensors never report filament", "[fsensor]") {
    for (const State state : { State::NotInitialized, State::NotCalibrated, State::NotConnected, State::Disabled }) {
        CHECK_FALSE(has_filament_surely_in_path(state, std::nullopt));
        CHECK_FALSE(has_filament_surely_in_path(std::nullopt, state));
    }
}
