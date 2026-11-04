#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <marlin_server_state.h>

using marlin_server::print_abort_target;
using marlin_server::State;
using marlin_server::state_after_resume_pickup;

TEST_CASE("print_abort accepts the states the INDX resume pickup blocks in") {
    // The pickup failure dialog calls print_abort() while the server is still in these states
    const auto state = GENERATE(State::Resuming_Begin, State::PowerPanic_FinishIndxToolchange);
    CHECK(print_abort_target(state) == State::Aborting_Begin);
}

TEST_CASE("print_abort targets of the other states") {
    CHECK(print_abort_target(State::Printing) == State::Aborting_Begin);
    CHECK(print_abort_target(State::Paused) == State::Aborting_Begin);
    CHECK(print_abort_target(State::Resuming_Reheating) == State::Aborting_Begin);
    CHECK(print_abort_target(State::PowerPanic_AwaitingResume) == State::Aborting_Begin);
    CHECK(print_abort_target(State::CrashRecovery_Tool_Pickup) == State::Aborting_Begin);
    CHECK(print_abort_target(State::PrintPreviewImage) == State::Aborting_Preview);
    CHECK(print_abort_target(State::PrintPreviewToolsMapping) == State::Aborting_Preview);

    CHECK_FALSE(print_abort_target(State::Idle).has_value());
    CHECK_FALSE(print_abort_target(State::Aborting_Begin).has_value());
    CHECK_FALSE(print_abort_target(State::Resuming_UnparkHead_XY).has_value());
    CHECK_FALSE(print_abort_target(State::CrashRecovery_Begin).has_value());
}

TEST_CASE("Resume continues only after a successful tool pickup") {
    CHECK(state_after_resume_pickup(State::Resuming_Begin, true) == State::Resuming_Begin);
}

TEST_CASE("Failed resume tool pickup goes back to pause") {
    // Absent nozzle or failed homing: pickup returns false without any other transition
    CHECK(state_after_resume_pickup(State::Resuming_Begin, false) == State::Paused);
}

TEST_CASE("Abort confirmed during the resume tool pickup is kept") {
    // The failure dialog's Abort runs print_abort() inside the blocking pickup
    const auto aborted = *print_abort_target(State::Resuming_Begin);
    const bool picked = GENERATE(false, true);
    CHECK(state_after_resume_pickup(aborted, picked) == State::Aborting_Begin);
}
