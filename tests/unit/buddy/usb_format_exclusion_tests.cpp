#include <usb_format_exclusion.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>

namespace {

constexpr auto drive_idle = [] { return false; };
constexpr auto drive_in_use = [] { return true; };

} // namespace

TEST_CASE("A format refuses while the drive is in use") {
    UsbFormatExclusion exclusion;
    CHECK_FALSE(exclusion.begin_format(drive_in_use));
    CHECK_FALSE(exclusion.is_formatting());
    CHECK(exclusion.begin_format(drive_idle));
    CHECK(exclusion.is_formatting());
}

TEST_CASE("A print start announced before the format holds it off") {
    UsbFormatExclusion exclusion;
    REQUIRE(exclusion.begin_print_start());

    // The print state does not show the print yet
    CHECK_FALSE(exclusion.begin_format(drive_idle));
    CHECK_FALSE(exclusion.is_formatting());

    exclusion.end_print_start();
    CHECK(exclusion.begin_format(drive_idle));
}

TEST_CASE("A print does not start during a format") {
    UsbFormatExclusion exclusion;
    REQUIRE(exclusion.begin_format(drive_idle));
    CHECK_FALSE(exclusion.begin_print_start());

    exclusion.end_format();
    CHECK(exclusion.begin_print_start());
}

TEST_CASE("Only one format at a time") {
    UsbFormatExclusion exclusion;
    REQUIRE(exclusion.begin_format(drive_idle));
    CHECK_FALSE(exclusion.begin_format(drive_idle));
    // The refused one does not end the running one
    CHECK(exclusion.is_formatting());
    CHECK_FALSE(exclusion.begin_print_start());
}

TEST_CASE("A print start and a format racing never both go ahead") {
    static constexpr int rounds = 20000;
    int both = 0;
    int print_starts = 0;
    int formats = 0;

    for (int round = 0; round < rounds; round++) {
        UsbFormatExclusion exclusion;
        std::atomic<bool> go = false;
        bool print_started = false;
        bool formatted = false;

        // The print state never shows the print here, so the announcement alone must hold the format off
        std::thread printer([&] {
            while (!go) {
            }
            print_started = exclusion.begin_print_start();
        });
        std::thread formatter([&] {
            while (!go) {
            }
            formatted = exclusion.begin_format(drive_idle);
        });
        go = true;
        printer.join();
        formatter.join();

        both += print_started && formatted;
        print_starts += print_started;
        formats += formatted;
    }

    // Both may give up when they overlap, so how many went ahead is not checked
    INFO(print_starts << " print starts and " << formats << " formats went ahead");
    CHECK(both == 0);
}
