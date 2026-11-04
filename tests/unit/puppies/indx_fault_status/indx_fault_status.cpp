#include <puppies/indx_fault_status.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstdint>
#include <utility>

using buddy::puppies::indx_fault_status::fatal_error_code;
using indx_head::errors::FaultStatusMask;

namespace {

FaultStatusMask operator|(FaultStatusMask a, FaultStatusMask b) {
    return static_cast<FaultStatusMask>(std::to_underlying(a) | std::to_underlying(b));
}

} // namespace

TEST_CASE("INDX head nozzle thermal faults stop the printer", "[indx_fault_status]") {
    CHECK(fatal_error_code(FaultStatusMask::nozzle_max_temp) == ErrCode::ERR_TEMPERATURE_HOTEND_MAXTEMP_ERROR);
    CHECK(fatal_error_code(FaultStatusMask::nozzle_min_temp) == ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR);
    CHECK(fatal_error_code(FaultStatusMask::tpis_invalid_timeout) == ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR);
}

TEST_CASE("INDX head board and ambient thermal faults stop the printer", "[indx_fault_status]") {
    CHECK(fatal_error_code(FaultStatusMask::board_min_temp) == ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MINTEMP_ERR);
    CHECK(fatal_error_code(FaultStatusMask::board_max_temp) == ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MAXTEMP_ERR);
    CHECK(fatal_error_code(FaultStatusMask::tpis_ambient_min_temp) == ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MINTEMP_ERR);
    CHECK(fatal_error_code(FaultStatusMask::tpis_ambient_max_temp) == ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MAXTEMP_ERR);
}

TEST_CASE("INDX head non-thermal faults are only acknowledged", "[indx_fault_status]") {
    const auto fault = GENERATE(
        FaultStatusMask::no_fault,
        FaultStatusMask::undefined_error,
        FaultStatusMask::hard_fault,
        FaultStatusMask::stack_overflow,
        FaultStatusMask::assert_failed,
        FaultStatusMask::loadcell_crc_mismatch,
        FaultStatusMask::watchdog_reset,
        FaultStatusMask::pin_reset,
        FaultStatusMask::power_reset,
        FaultStatusMask::watchdog_reset | FaultStatusMask::pin_reset | FaultStatusMask::power_reset);
    CHECK_FALSE(fatal_error_code(fault).has_value());
}

TEST_CASE("INDX head thermal fault survives the reset it caused", "[indx_fault_status]") {
    // The head stalls into a watchdog reset after a thermal fault, so both bits come together
    CHECK(fatal_error_code(FaultStatusMask::nozzle_max_temp | FaultStatusMask::watchdog_reset) == ErrCode::ERR_TEMPERATURE_HOTEND_MAXTEMP_ERROR);
    CHECK(fatal_error_code(FaultStatusMask::nozzle_min_temp | FaultStatusMask::watchdog_reset) == ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR);
    CHECK(fatal_error_code(FaultStatusMask::tpis_invalid_timeout | FaultStatusMask::watchdog_reset | FaultStatusMask::power_reset) == ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR);
}

TEST_CASE("INDX head combined thermal faults report the highest priority one", "[indx_fault_status]") {
    CHECK(fatal_error_code(FaultStatusMask::nozzle_min_temp | FaultStatusMask::nozzle_max_temp) == ErrCode::ERR_TEMPERATURE_HOTEND_MAXTEMP_ERROR);
    CHECK(fatal_error_code(FaultStatusMask::nozzle_max_temp | FaultStatusMask::tpis_invalid_timeout) == ErrCode::ERR_TEMPERATURE_HOTEND_MAXTEMP_ERROR);
    CHECK(fatal_error_code(FaultStatusMask::nozzle_min_temp | FaultStatusMask::tpis_invalid_timeout) == ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR);
    CHECK(fatal_error_code(FaultStatusMask::nozzle_max_temp | FaultStatusMask::board_min_temp) == ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MINTEMP_ERR);
    CHECK(fatal_error_code(FaultStatusMask::board_min_temp | FaultStatusMask::board_max_temp) == ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MAXTEMP_ERR);
    CHECK(fatal_error_code(FaultStatusMask::board_max_temp | FaultStatusMask::tpis_ambient_min_temp) == ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MINTEMP_ERR);
    CHECK(fatal_error_code(FaultStatusMask::tpis_ambient_min_temp | FaultStatusMask::tpis_ambient_max_temp) == ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MAXTEMP_ERR);
}

TEST_CASE("INDX head fault mask is fatal exactly when it has a thermal bit", "[indx_fault_status]") {
    for (uint32_t mask = 1; mask <= 0x7fff; ++mask) {
        const auto fault = static_cast<FaultStatusMask>(mask);
        constexpr uint16_t thermal_bits = std::to_underlying(FaultStatusMask::nozzle_min_temp)
            | std::to_underlying(FaultStatusMask::tpis_invalid_timeout)
            | std::to_underlying(FaultStatusMask::nozzle_max_temp)
            | std::to_underlying(FaultStatusMask::board_min_temp)
            | std::to_underlying(FaultStatusMask::board_max_temp)
            | std::to_underlying(FaultStatusMask::tpis_ambient_min_temp)
            | std::to_underlying(FaultStatusMask::tpis_ambient_max_temp);
        REQUIRE(fatal_error_code(fault).has_value() == ((mask & thermal_bits) != 0));
    }
}
