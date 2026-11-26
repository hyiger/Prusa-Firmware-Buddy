#pragma once

#include <inc/macros.h>

// Fake marlin config, good enough for use xy_pos_t from types.h
#define Z_DRIVER_TYPE

#define HOTENDS         1
#define EXTRUDERS       9 // 8 + NoTool, as on the Core One INDX
#define SWITCH_ENABLED_ 1
#define SINGLENOZZLE

#define LOGICAL_AXES 4
#define NUM_AXES     3
#define E_STEPPERS   9

// Crash detection tunables (matching real firmware config)
#define CRASH_STALL_GUARD \
    { 2, 2 }
#define CRASH_MAX_PERIOD \
    { 210, 210 }
#define CRASH_FILTER (false)
