#include "test_files.hpp"

#include <gcode_reader_any.hpp>
#include <gcode_info.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string_view>

using Catch::Matchers::WithinAbs;

namespace {

void load(GCodeInfo &info, const char *filename) {
    AnyGcodeFormatReader reader(filename);
    REQUIRE(reader.is_open());
    info.load(*reader.get());
    REQUIRE(info.is_loaded());
}

} // namespace

static_assert(EXTRUDERS == 9);

TEST_CASE("GCodeInfo reads per-tool lists longer than the line buffer") {
    GCodeInfo info;
    load(info, EIGHT_TOOLS);

    static constexpr float used_mm[] = { 12345.67f, 23456.78f, 34567.89f, 45678.90f, 56789.01f, 67890.12f, 78901.23f, 89012.34f };
    static constexpr float used_g[] = { 36.82f, 69.96f, 103.09f, 136.23f, 169.37f, 202.50f, 235.64f, 268.78f };
    static constexpr std::string_view types[] = { "PLA", "PETG", "ASA", "PLA", "PETG", "ASA", "PLA", "PETG" };
    static constexpr uint32_t colors[] = { 0xFF8000, 0xFFFFFF, 0x000000, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF };

    CHECK(info.UsedExtrudersCount() == 8);
    for (uint8_t tool = 0; tool < 8; tool++) {
        INFO("tool " << int(tool));
        const auto &extruder = info.get_extruder_info(tool);
        REQUIRE(extruder.filament_used_mm.has_value());
        CHECK_THAT(*extruder.filament_used_mm, WithinAbs(used_mm[tool], 0.01));
        // The rest of a long G-code line, which looks like metadata, is not read as a line of its own
        REQUIRE(extruder.filament_used_g.has_value());
        CHECK_THAT(*extruder.filament_used_g, WithinAbs(used_g[tool], 0.01));
        CHECK(std::string_view(extruder.filament_name) == types[tool]);
        REQUIRE(extruder.extruder_colour.has_value());
        CHECK(*extruder.extruder_colour == Color::from_raw(colors[tool]));
    }
}

TEST_CASE("GCodeInfo drops the value cut off at the end of a too long metadata line") {
    GCodeInfo info;
    load(info, METADATA_TOO_LONG);

    for (uint8_t tool = 0; tool < 5; tool++) {
        INFO("tool " << int(tool));
        REQUIRE(info.get_extruder_info(tool).filament_used_mm.has_value());
        CHECK_THAT(*info.get_extruder_info(tool).filament_used_mm, WithinAbs(1111111111111111.5, 1e9));
    }
    for (uint8_t tool = 5; tool < 8; tool++) {
        INFO("tool " << int(tool));
        CHECK(!info.get_extruder_info(tool).filament_used_mm.has_value());
    }
    // The line after it is read whole
    CHECK_THAT(info.get_extruder_info(7).filament_used_g.value_or(0), WithinAbs(8, 0.001));
}
