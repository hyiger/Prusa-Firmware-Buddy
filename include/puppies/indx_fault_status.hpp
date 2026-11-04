#pragma once

#include <error_codes.hpp>
#include <indx_head/errors.hpp>

#include <optional>
#include <utility>

namespace buddy::puppies::indx_fault_status {

/// Error that a fault status reported by the INDX head must stop the printer with, if any.
///
/// The head shuts its heater off and resets itself on every thermal fault. Recovering
/// the head re-applies the heating target, so a thermal fault must not be treated as
/// a mere head reset. Faults without an error here (resets, firmware faults) are only
/// logged and acknowledged.
///
/// When several faults are set, the one with the highest priority (see FaultStatusMask) wins.
constexpr std::optional<ErrCode> fatal_error_code(indx_head::errors::FaultStatusMask fault) {
    using indx_head::errors::FaultStatusMask;

    struct FatalFault {
        FaultStatusMask fault;
        ErrCode error;
    };

    // Highest priority first
    constexpr FatalFault fatal_faults[] = {
        { FaultStatusMask::tpis_ambient_max_temp, ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MAXTEMP_ERR },
        { FaultStatusMask::tpis_ambient_min_temp, ErrCode::ERR_TEMPERATURE_INDX_HEAD_TPIS_AMBIENT_MINTEMP_ERR },
        { FaultStatusMask::board_max_temp, ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MAXTEMP_ERR },
        { FaultStatusMask::board_min_temp, ErrCode::ERR_TEMPERATURE_INDX_HEAD_BOARD_MINTEMP_ERR },
        { FaultStatusMask::nozzle_max_temp, ErrCode::ERR_TEMPERATURE_HOTEND_MAXTEMP_ERROR },
        // No valid nozzle reading means the nozzle temperature is unknown, a sensor failure like MINTEMP
        { FaultStatusMask::tpis_invalid_timeout, ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR },
        { FaultStatusMask::nozzle_min_temp, ErrCode::ERR_TEMPERATURE_HOTEND_MINTEMP_ERROR },
    };

    for (const FatalFault &fatal_fault : fatal_faults) {
        if (std::to_underlying(fault) & std::to_underlying(fatal_fault.fault)) {
            return fatal_fault.error;
        }
    }
    return std::nullopt;
}

} // namespace buddy::puppies::indx_fault_status
