#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <ubl_g29_phases.hpp>

TEST_CASE("G29 phases that probe need a picked tool") {
    // P1 probes the mesh, P9 taps the bed to clean the nozzle, P10 probes a single point
    const int phase = GENERATE(1, 9, 10);
    CHECK(ubl_g29_phase_probes(phase));
}

TEST_CASE("G29 phases that only edit the mesh work without a tool") {
    // Zero, LCD-only, fill, edit, mean adjustment and shift
    const int phase = GENERATE(0, 2, 3, 4, 5, 6);
    CHECK_FALSE(ubl_g29_phase_probes(phase));
}
