#include <puppies/nozzle_presence_cache.hpp>

#include <catch2/catch_test_macros.hpp>

using buddy::puppies::NozzlePresenceCache;
using indx_head::NozzlePresence;

TEST_CASE("nozzle_presence_cache: report needs the current token", "[indx]") {
    NozzlePresenceCache cache;
    cache.invalidate(100);
    CHECK(cache.token() == 100);
    CHECK(cache.get() == std::nullopt);

    cache.publish(NozzlePresence::present, 99);
    CHECK(cache.get() == std::nullopt);

    cache.publish(NozzlePresence::unknown, 100);
    CHECK(cache.get() == std::nullopt);

    cache.publish(NozzlePresence::present, 100);
    CHECK(cache.get() == true);

    cache.publish(NozzlePresence::absent, 100);
    CHECK(cache.get() == false);
}

TEST_CASE("nozzle_presence_cache: invalidation hides the previous result", "[indx]") {
    NozzlePresenceCache cache;
    cache.invalidate(100);
    cache.publish(NozzlePresence::present, 100);
    REQUIRE(cache.get() == true);

    cache.invalidate(200);
    CHECK(cache.get() == std::nullopt);
}

TEST_CASE("nozzle_presence_cache: report validated before invalidation is not published after it", "[indx]") {
    // The status handler checks a report acknowledging token 100 while 100 is current, and only
    // stores it after the Marlin task has invalidated to token 200.
    NozzlePresenceCache cache;
    cache.invalidate(100);
    REQUIRE(cache.token() == 100);

    cache.invalidate(200);
    cache.publish(NozzlePresence::present, 100);
    CHECK(cache.get() == std::nullopt);

    cache.publish(NozzlePresence::unknown, 200);
    CHECK(cache.get() == std::nullopt);

    cache.publish(NozzlePresence::absent, 200);
    CHECK(cache.get() == false);
}

TEST_CASE("nozzle_presence_cache: initial token matches the head's initial ack", "[indx]") {
    NozzlePresenceCache cache;
    CHECK(cache.token() == 0);
    CHECK(cache.get() == std::nullopt);

    cache.publish(NozzlePresence::present, 0);
    CHECK(cache.get() == true);
}
