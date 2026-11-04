#include <serial_printing.hpp>

#include <catch2/catch_test_macros.hpp>

namespace {

uint32_t now_ms = 0;
bool commands_queued = false;
bool planner_busy = false;
int serial_print_start_calls = 0;
int serial_print_finalize_calls = 0;

constexpr uint32_t timeout_ms = 5000;

void reset_fakes(uint32_t start_ms) {
    now_ms = start_ms;
    commands_queued = false;
    planner_busy = false;
    serial_print_start_calls = 0;
    serial_print_finalize_calls = 0;
}

} // namespace

uint32_t ticks_ms() {
    return now_ms;
}

bool GCodeQueue::pause_serial_commands = false;

bool GCodeQueue::has_commands_queued() {
    return commands_queued;
}

bool Planner::processing() const {
    return planner_busy;
}

Planner planner;

namespace printer_state {
bool remote_print_ready(bool) {
    return true;
}
} // namespace printer_state

namespace marlin_server {
void enqueue_gcode(const char *) {}

void serial_print_start() {
    ++serial_print_start_calls;
}

void serial_print_finalize() {
    ++serial_print_finalize_calls;
}
} // namespace marlin_server

TEST_CASE("Serial print timeout starts when printing starts", "[serial_printing]") {
    reset_fakes(1000);

    // A blocking print-indicating command arms the serial print. While it
    // runs, the print cannot enter the printing state.
    SerialPrinting::serial_command_hook("M109 S290");
    REQUIRE(serial_print_start_calls == 1);
    now_ms += 3 * timeout_ms;

    // The command finished; the host has not sent the next line yet.
    SerialPrinting::print_started();
    now_ms += 1;
    SerialPrinting::print_loop();
    CHECK(serial_print_finalize_calls == 0);

    now_ms += timeout_ms - 1;
    SerialPrinting::print_loop();
    CHECK(serial_print_finalize_calls == 0);

    now_ms += 2;
    SerialPrinting::print_loop();
    CHECK(serial_print_finalize_calls == 1);
}

TEST_CASE("Serial print timeout is refreshed by activity", "[serial_printing]") {
    reset_fakes(1000);

    SerialPrinting::serial_command_hook("N12 G1 X10");
    REQUIRE(serial_print_start_calls == 1);
    SerialPrinting::print_started();

    SECTION("queued commands") {
        commands_queued = true;
    }
    SECTION("planner motion") {
        planner_busy = true;
    }

    now_ms += 2 * timeout_ms;
    SerialPrinting::print_loop();
    CHECK(serial_print_finalize_calls == 0);

    commands_queued = false;
    planner_busy = false;
    now_ms += timeout_ms - 1;
    SerialPrinting::print_loop();
    CHECK(serial_print_finalize_calls == 0);

    now_ms += 2;
    SerialPrinting::print_loop();
    CHECK(serial_print_finalize_calls == 1);
}

TEST_CASE("Only print-indicating commands start a serial print", "[serial_printing]") {
    reset_fakes(1000);

    SerialPrinting::serial_command_hook("M104 S200");
    SerialPrinting::serial_command_hook("N3 M115");
    CHECK(serial_print_start_calls == 0);

    SerialPrinting::serial_command_hook("M190 S60");
    SerialPrinting::serial_command_hook("N4 M73 P10");
    SerialPrinting::serial_command_hook("G28");
    CHECK(serial_print_start_calls == 3);
}
