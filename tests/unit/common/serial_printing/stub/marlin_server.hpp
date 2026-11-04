#pragma once

// The real header provides atoi() transitively.
#include <cstdlib>

namespace marlin_server {
void enqueue_gcode(const char *gcode);
void serial_print_start();
void serial_print_finalize();
} // namespace marlin_server
