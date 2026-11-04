#pragma once

/// Whether UBL G29 P<phase> touches the bed with the nozzle and so needs a physical tool picked.
constexpr bool ubl_g29_phase_probes(int phase) {
    switch (phase) {
    case 1: // Automatic mesh probing
    case 9: // Probe cleanup
    case 10: // Single point probe
        return true;
    default:
        return false;
    }
}
