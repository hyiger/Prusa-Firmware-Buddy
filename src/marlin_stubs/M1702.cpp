#include <option/has_coldpull.h>
#include <option/has_gui.h>
#include <option/has_indx.h>

#include <M70X.hpp>
#include <fs_autoload_autolock.hpp>
#include <fs_event_autolock.hpp>

#include <module/temperature.h>
#include <Marlin.h>

#include <common/cold_pull.hpp>
#include <client_fsm_types.hpp>
#include <client_response.hpp>
#include <common/marlin_server.hpp>
#include <raii/auto_restore.hpp>
#include <mapi/cold_extrude.hpp>
#include <utils/variant_utils.hpp>
#include <mapi/motion.hpp>
#include <feature/motor_current_profile/motor_current_profile.hpp>
#include <feature/prusa/e-stall_detector.h>
#include <raii/scope_guard.hpp>

#include <option/has_auto_retract.h>
#if HAS_AUTO_RETRACT()
    #include <feature/auto_retract/auto_retract.hpp>
#endif

#include <option/has_mmu2.h>
#if HAS_MMU2()
    #include <feature/prusa/MMU2/mmu2_mk4.h>
#endif

#include <option/has_toolchanger.h>
#if HAS_TOOLCHANGER()
    #include <module/prusa/toolchanger.h>
#endif

#if HAS_INDX()
    #include <mapi/parking.hpp>
    #include <nozzle_cleaner.hpp>
#endif

#include <optional>
#include <bsod/bsod.h>

namespace PrusaGcodeSuite {

#if HAS_COLDPULL()

namespace {

    constexpr const uint16_t HOTEND_COLD_TEMP { 36 };
    // Temperature was increased from 80 to 90 to make it easier to pull out
    // Slightly higher temperature less likely create big blobs, that cannot be manually pulled from the idler
    constexpr const uint16_t HOTEND_PULL_TEMP { 90 };
    constexpr const uint16_t HOTEND_END_TEMP { 95 };

    #if HAS_INDX()
    constexpr const uint16_t INDX_FLUSH_TEMP { 290 };
    constexpr const uint16_t INDX_PACK_TARGET_TEMP { 180 };
    constexpr const uint16_t INDX_COOL_TEMP { 60 };
    constexpr const uint16_t INDX_PULL_TEMP { 80 };
    // Softens PLA residue so the wiper takes it off instead of dragging cold strings.
    constexpr const uint16_t INDX_WIPE_TEMP { 170 };
    #endif

    constexpr const millis_t TIMEOUT_DISABLED { 0 };
    constexpr const millis_t COOLING_TIMEOUT_MILLIS { 1000 * 60 * 15 };
    constexpr const millis_t PROGRESS_MILLIS { 500 }; // in ms

    #if HAS_TOOLCHANGER()
    PhysicalToolIndex selected_tool = PhysicalToolIndex::from_raw(0);
    #endif

    #if HAS_MMU2()
    bool was_mmu_enabled { false };
    #endif

    bool was_success { false };

    using marlin_server::wait_for_response;

    template <typename CMP, typename CBK>
    Response wait_while_with_progress(
        const PhasesColdPull phase, const millis_t timeout, CMP &&compare, CBK &&progress) {

        millis_t now = millis();

        const millis_t deadline = now + timeout;
        millis_t progress_timeout { now };

        while (compare()) {
            if (Response response = marlin_server::get_response_from_phase(phase); response != Response::_none) {
                return response;
            }

            now = millis();

            if (timeout && ELAPSED(now, deadline)) {
                break;
            }
            if (ELAPSED(now, progress_timeout)) {
                progress(deadline - now);
                progress_timeout = now + PROGRESS_MILLIS;
            }
            idle(true);
        }
        return Response::_none;
    }

    /// Heats the picked tool to @p target_hotend_temp and waits for @p reached_temp, by default the target the
    /// heater accepted, which is clamped below the hotend's maximum.
    /// @return false on abort
    bool heat_up(uint16_t target_hotend_temp, PhasesColdPull current_phase, std::optional<uint16_t> reached_temp = std::nullopt);

    PhasesColdPull info() {
        switch (wait_for_response(PhasesColdPull::introduction)) {
        case Response::Stop:
            return PhasesColdPull::cleanup;
        case Response::Continue:
    #if HAS_TOOLCHANGER()
            return prusa_toolchanger.is_toolchanger_enabled() ? PhasesColdPull::select_tool : PhasesColdPull::unload_ptfe;
    #elif HAS_MMU2()
            return MMU2::mmu2.Enabled() ? PhasesColdPull::unload_ptfe : PhasesColdPull::prepare_filament;
    #else
            // #error dead code found by automatic analyses (see BFW-5461)
            return PhasesColdPull::prepare_filament;
    #endif
        default:
            bsod_unreachable();
        }
    }

    #if HAS_TOOLCHANGER()
    PhasesColdPull select_tool() {
        const auto r = marlin_server::wait_for_response_variant(PhasesColdPull::select_tool);
        if (auto tool = r.value_maybe<PhysicalToolIndex>()) {
            selected_tool = *tool;
            return PhasesColdPull::pick_tool;

        } else {
            bsod_unreachable();
        }
    }

    PhasesColdPull pick_tool() {
        [[maybe_unused]] const bool picked = tool_change(selected_tool, tool_return_t::no_return, tool_change_lift_t::full_lift, 1);
        #if HAS_INDX()
        // Every later step heats and moves this tool's extruder.
        if (!picked) {
            return PhasesColdPull::cleanup;
        }

        // A remembered auto-retract would cause the planner to reinterpret the
        // deliberate cold-pull extrusion moves. This procedure establishes a
        // new physical filament state, so invalidate the remembered value.
        buddy::auto_retract().set_retracted_distance(selected_tool, std::nullopt);

        // Put the selected tool somewhere comfortable to work on before asking
        // the user to remove its PTFE tube. Positive Z moves the bed down. The
        // Relative parking coordinate is clamped automatically to Z_MAX_POS.
        mapi::home_if_needed_and_park(mapi::ParkingPosition {
            .x = static_cast<float>(X_CENTER),
            .y = static_cast<float>(Y_CENTER),
            .z = mapi::ParkingPosition::Relative { .delta = 20.f },
        });
        #endif
        return PhasesColdPull::unload_ptfe;
    }
    #endif

    #if HAS_MMU2()

    PhasesColdPull stop_mmu() {
        if (MMU2::mmu2.Enabled() == true) {

            // Two reasons for the show_time:
            // - show the screen for at least 1.5 seconds (it's almost instant and unreadable otherwise)
            // - must wait a bit while the new MMU state propagates everywhere
            constexpr const millis_t MINIMAL_SHOW_TIME { 1500 };
            const millis_t deadline = millis() + MINIMAL_SHOW_TIME;

            auto progress = [](auto) {}; // intentionally empty
            auto mmu_on_timed = [&]() {
                return MMU2::mmu2.Enabled() == true || millis() < deadline;
            };

            filament_gcodes::mmu_off();

            switch (wait_while_with_progress(PhasesColdPull::stop_mmu, 0, mmu_on_timed, progress)) {
            case Response::Abort:
                return PhasesColdPull::cleanup;
            case Response::_none:
                break;
            default:
                bsod_unreachable();
            }

            idle(true); // Still do one event-loop in case the MMU stop took too long.
        }

        return PhasesColdPull::blank_load;
    }

    PhasesColdPull cleanup() {
        if (was_mmu_enabled && MMU2::mmu2.Enabled() == false) {

            auto progress = [](auto) {}; // intentionally empty
            auto mmu_off = []() {
                return MMU2::mmu2.Enabled() == false;
            };

            filament_gcodes::mmu_on();

            idle(true);
            wait_while_with_progress(PhasesColdPull::cleanup, 0, mmu_off, progress);
        }

        return was_success ? PhasesColdPull::pull_done : PhasesColdPull::finish;
    }

    #endif

    PhasesColdPull unload_ptfe() {
        switch (wait_for_response(PhasesColdPull::unload_ptfe)) {
    #if !HAS_INDX()
        case Response::Unload:
            return PhasesColdPull::blank_unload;
    #endif
        case Response::Continue:
    #if HAS_INDX()
            return PhasesColdPull::prepare_filament;
    #else
            return PhasesColdPull::load_ptfe;
    #endif
        case Response::Abort:
            return PhasesColdPull::cleanup;
        default:
            bsod_unreachable();
        }
    }

    PhasesColdPull load_ptfe() {
        switch (wait_for_response(PhasesColdPull::load_ptfe)) {
        case Response::Load:
    #if HAS_MMU2()
            return was_mmu_enabled ? PhasesColdPull::stop_mmu : PhasesColdPull::blank_load;
    #else
            return PhasesColdPull::blank_load;
    #endif
        case Response::Continue:
    #if HAS_INDX()
            return PhasesColdPull::prepare_filament;
    #elif HAS_AUTO_RETRACT()
            return PhasesColdPull::deretract;
    #else
            return PhasesColdPull::cool_down;
    #endif
        case Response::Abort:
            return PhasesColdPull::cleanup;
        default:
            bsod_unreachable();
        }
    }

    PhasesColdPull prepare_filament() {
        switch (wait_for_response(PhasesColdPull::prepare_filament)) {
    #if !HAS_INDX()
        case Response::Unload:
            return PhasesColdPull::blank_unload;
        case Response::Load:
            return PhasesColdPull::blank_load;
    #endif
        case Response::Continue:
    #if HAS_INDX()
            return PhasesColdPull::flush;
    #elif HAS_AUTO_RETRACT()
            return PhasesColdPull::deretract;
    #else
            return PhasesColdPull::cool_down;
    #endif
        case Response::Abort:
            return PhasesColdPull::cleanup;
        default:
            bsod_unreachable();
        }
    }

    #if HAS_INDX()
    /// Queues an extruder move and waits for it. False when it didn't complete: the planner refused it, or a
    /// stop or motion fault discarded it, after which the planner drains until this command ends.
    bool extrude_and_wait(float distance, float feed_rate) {
        const bool queued = mapi::extruder_move(distance, feed_rate);
        planner.synchronize();
        return queued && !planner.draining();
    }

    /// An Abort pressed during a move; the next phase change would discard it.
    bool abort_requested(PhasesColdPull phase) {
        return marlin_server::get_response_from_phase(phase) == Response::Abort;
    }

    bool delay_abortable(PhasesColdPull phase, millis_t duration) {
        const millis_t deadline = millis() + duration;
        auto waiting = [deadline]() { return PENDING(millis(), deadline); };
        auto no_progress = [](millis_t) {};
        return wait_while_with_progress(phase, duration, waiting, no_progress) != Response::Abort;
    }

    PhasesColdPull flush() {
        return heat_up(INDX_FLUSH_TEMP, PhasesColdPull::flush) ? PhasesColdPull::purge_ready : PhasesColdPull::cleanup;
    }

    // The screen and the nozzle can't be watched at once, so tell the user what to look for before purging.
    PhasesColdPull purge_ready() {
        switch (wait_for_response(PhasesColdPull::purge_ready)) {
        case Response::Continue:
            return PhasesColdPull::purge;
        case Response::Abort:
            return PhasesColdPull::cleanup;
        default:
            bsod_unreachable();
        }
    }

    PhasesColdPull purge() {
        // Prompts wait without limit, and the safety timer turns the heater off after a while.
        if (!heat_up(INDX_FLUSH_TEMP, PhasesColdPull::purge)) {
            return PhasesColdPull::cleanup;
        }

        BlockEStallDetection estall_blocker;
        for (uint8_t i = 0; i < 3; ++i) {
            if (!extrude_and_wait(20, 2)) {
                return PhasesColdPull::cleanup;
            }
            if (abort_requested(PhasesColdPull::purge) || (i != 2 && !delay_abortable(PhasesColdPull::purge, 2000))) {
                return PhasesColdPull::cleanup;
            }
        }
        return PhasesColdPull::tip_check;
    }

    PhasesColdPull tip_check() {
        switch (wait_for_response(PhasesColdPull::tip_check)) {
        case Response::Continue:
            return PhasesColdPull::pack;
        case Response::Abort:
            return PhasesColdPull::cleanup;
        default:
            bsod_unreachable();
        }
    }

    PhasesColdPull pack() {
        // Pack from the flush temperature, even after a long wait at the tip check.
        if (!heat_up(INDX_FLUSH_TEMP, PhasesColdPull::pack)) {
            return PhasesColdPull::cleanup;
        }
        thermalManager.setTargetHotend(INDX_PACK_TARGET_TEMP, selected_tool);
        // The last pushes can come close to the minimum extrusion temperature on the way down.
        mapi::ColdExtrudeGuard cold_extrude_guard;
        BlockEStallDetection estall_blocker;
        for (uint8_t i = 0; i < 8; ++i) {
            if (!extrude_and_wait(2, 1)) {
                return PhasesColdPull::cleanup;
            }
            if (abort_requested(PhasesColdPull::pack) || (i != 7 && !delay_abortable(PhasesColdPull::pack, 5000))) {
                return PhasesColdPull::cleanup;
            }
        }
        return PhasesColdPull::cool_down;
    }
    #endif

    PhasesColdPull blank_unload() {
        auto active_tool = stdext::get_optional<VirtualToolIndex>(VirtualToolIndex::currently_selected());
        if (!active_tool.has_value()) {
            bsod("no virtual tool to unload");
        }

        filament_gcodes::M702_unload(
            std::nullopt,
            Z_AXIS_UNLOAD_POS,
            RetAndCool_t::Return,
            *active_tool,
    #if HAS_MMU2()
            !MMU2::mmu2.Enabled() // MUST be false when MMU is enabled otherwise unload wont do full length
    #else
            true
    #endif
        );
        planner.resume_queuing(); // HACK for planner.quick_stop(); in Pause::check_user_stop()

    #if HAS_TOOLCHANGER() || HAS_MMU2()
        return PhasesColdPull::load_ptfe;
    #else
        // #error dead code found by automatic analyses (see BFW-5461)
        return PhasesColdPull::prepare_filament;
    #endif
    }

    PhasesColdPull blank_load() {
        auto active_tool = stdext::get_optional<VirtualToolIndex>(VirtualToolIndex::currently_selected());
        if (!active_tool.has_value()) {
            bsod("no virtual tool to load");
        }

        filament_gcodes::M701_load(filament_gcodes::M701LoadArgs {
            .filament_to_be_loaded = PresetFilamentType::PLA,
            .z_min_pos = Z_AXIS_LOAD_POS,
            .op_preheat = RetAndCool_t::Return,
            .virtual_tool = *active_tool,
        });
        planner.resume_queuing(); // HACK for planner.quick_stop(); in Pause::check_user_stop()

        switch (PreheatStatus::ConsumeResult()) {
        case PreheatStatus::Result::DoneHasFilament:
    #if HAS_AUTO_RETRACT()
            return PhasesColdPull::deretract;
    #else
            return PhasesColdPull::cool_down;
    #endif
        default:
    #if HAS_TOOLCHANGER() || HAS_MMU2()
            return PhasesColdPull::load_ptfe;
    #else
            // #error dead code found by automatic analyses (see BFW-5461)
            return PhasesColdPull::prepare_filament;
    #endif
        }
    }

    PhasesColdPull cool_down() {
        auto active_tool = stdext::get_optional<PhysicalToolIndex>(PhysicalToolIndex::currently_selected());
        if (!active_tool.has_value()) {
            bsod_unreachable();
        }

        thermalManager.disable_hotend(); // cool down without target to avoid PID handling target temp

        // This is a legit use (is it?)
        constexpr uint16_t cold_temp = [] {
    #if HAS_INDX()
            return INDX_COOL_TEMP;
    #else
            return HOTEND_COLD_TEMP;
    #endif
        }();
        marlin_server::call_manually::set_temp_to_display(cold_temp, *active_tool); // still show target temperature

        auto too_hot = [active_tool, cold_temp]() {
            return static_cast<uint16_t>(Temperature::degHotend(*active_tool)) > cold_temp;
        };

        auto progress = [active_tool, cold_temp](const millis_t time_left) {
            cold_pull::TemperatureProgressData data { { 0 } };
            data.percent = static_cast<uint8_t>(
                100.0f * cold_temp / Temperature::degHotend(*active_tool));
            data.time_sec = time_left / 1000;
            marlin_server::fsm_change(PhasesColdPull::cool_down, data.fsm_data);
        };

        const auto fan_speed_stored = Temperature::print_fan_speed;
        ScopeGuard restore_fan([fan_speed_stored]() {
            thermalManager.set_print_fan_speed(fan_speed_stored);
        });
    #if HAS_INDX()
        thermalManager.set_print_fan_speed(255);
    #else
        thermalManager.set_print_fan_speed(240);
    #endif

        switch (wait_while_with_progress(PhasesColdPull::cool_down, COOLING_TIMEOUT_MILLIS, too_hot, progress)) {
        case Response::Abort:
            return PhasesColdPull::cleanup;
        case Response::_none:
            break;
        default:
            bsod_unreachable();
        }
    #if HAS_INDX()
        cold_pull::TemperatureProgressData complete { { 0 } };
        complete.percent = 100;
        marlin_server::fsm_change(PhasesColdPull::cool_down, complete.fsm_data);
        marlin_server::call_manually::set_temp_to_display(0, *active_tool);
        return PhasesColdPull::cooling_dwell;
    #else
        return PhasesColdPull::heat_up;
    #endif
    }

    #if HAS_INDX()
    PhasesColdPull cooling_dwell() {
        constexpr millis_t dwell_duration = 120000;
        const millis_t deadline = millis() + dwell_duration;

        const auto fan_speed_stored = Temperature::print_fan_speed;
        ScopeGuard restore_fan([fan_speed_stored]() {
            thermalManager.set_print_fan_speed(fan_speed_stored);
        });
        thermalManager.set_print_fan_speed(255);

        auto waiting = [deadline]() { return PENDING(millis(), deadline); };
        auto progress = [](const millis_t time_left) {
            cold_pull::TemperatureProgressData data { { 0 } };
            data.percent = static_cast<uint8_t>(100 - std::min<millis_t>(100, time_left * 100 / dwell_duration));
            data.time_sec = static_cast<uint16_t>((time_left + 999) / 1000);
            marlin_server::fsm_change(PhasesColdPull::cooling_dwell, data.fsm_data);
        };

        switch (wait_while_with_progress(PhasesColdPull::cooling_dwell, dwell_duration, waiting, progress)) {
        case Response::Abort:
            return PhasesColdPull::cleanup;
        case Response::_none:
            return PhasesColdPull::heat_up;
        default:
            bsod_unreachable();
        }
    }
    #endif

    bool heat_up(const uint16_t target_hotend_temp, PhasesColdPull current_phase, std::optional<uint16_t> reached_temp) {
        auto active_tool = stdext::get_optional<PhysicalToolIndex>(PhysicalToolIndex::currently_selected());
        if (!active_tool.has_value()) {
            bsod_unreachable();
        }

        thermalManager.setTargetHotend(target_hotend_temp, *active_tool);
        const uint16_t wait_temp = reached_temp.value_or(Temperature::degTargetHotend(*active_tool));

        auto too_cold = [active_tool, wait_temp]() {
            return static_cast<uint16_t>(Temperature::degHotend(*active_tool)) < wait_temp;
        };

        auto progress = [&](millis_t) {
            cold_pull::TemperatureProgressData data { { 0 } };
            data.percent = static_cast<uint8_t>(
                std::min(100.0f, 100.0f * Temperature::degHotend(*active_tool) / wait_temp));
            marlin_server::fsm_change(current_phase, data.fsm_data);
        };

        switch (wait_while_with_progress(current_phase, TIMEOUT_DISABLED, too_cold, progress)) {
        case Response::Abort:
            return false;
        case Response::_none:
            break;
        default:
            bsod_unreachable();
        }

        return true;
    }

    #if HAS_INDX()
    /// Aimed straight at the pull temperature, the PID (not tuned this low) overshoots and takes about 30 s to
    /// settle. Stop the first stage just short, so the momentum bleeds off, then close the last degrees.
    bool heat_up_for_pull(PhasesColdPull phase) {
        return heat_up(INDX_PULL_TEMP - 3, phase, INDX_PULL_TEMP - 4) && heat_up(INDX_PULL_TEMP, phase);
    }
    #endif

    #if HAS_AUTO_RETRACT()
    PhasesColdPull deretract() {

        if (!buddy::auto_retract().will_deretract()) {
            return PhasesColdPull::cool_down;
        }

        auto filament_type = match(
            marlin_vars().active_extruder.get(),
            [](VirtualToolIndex virtual_tool) -> FilamentType { return FilamentType::for_tool_heuristic(virtual_tool); },
            [](NoTool) { return FilamentType::none; });
        // If loaded filament is unknown FilamentType::none has nozzle temperature 215 -> Correct default value since PLA is recommended in the dialog
        const auto hotend_detraction_temp = filament_type.parameters().nozzle_temperature;
        if (!heat_up(hotend_detraction_temp, PhasesColdPull::deretract)) {
            return PhasesColdPull::cleanup;
        }

        buddy::auto_retract().maybe_deretract_to_nozzle();
        return PhasesColdPull::cool_down;
    }
    #endif

    PhasesColdPull automatic_pull() {
        const auto virtual_tool = std::get<VirtualToolIndex>(VirtualToolIndex::currently_selected());

    #if HAS_INDX()
        // The prompt before the pull waits without limit: a plug that cooled back down could snap or strain the drive.
        heat_up_for_pull(PhasesColdPull::automatic_pull);
    #else
        thermalManager.setTargetHotend(HOTEND_END_TEMP, virtual_tool.to_physical());
    #endif

        {
    #if ENABLED(PREVENT_COLD_EXTRUSION)
            mapi::ColdExtrudeGuard cold_extrude_guard;
    #endif
    #if HAS_INDX()
            const auto previous_profile = buddy::active_motor_current_profile();
            ScopeGuard restore_current([previous_profile]() {
                buddy::set_active_motor_current_profile(previous_profile);
            });
            buddy::set_active_motor_current_profile(buddy::StandardMotorCurrentProfile::increased_e);
            BlockEStallDetection estall_blocker;
            const bool pulled = extrude_and_wait(-40, 50) && extrude_and_wait(-40, 20);
    #else
            mapi::extruder_move(-std::min(300, EXTRUDE_MAXLENGTH), 50);
    #endif
            planner.synchronize();

    #if HAS_INDX()
            // Without the full pull, the strand is still in the tool.
            if (!pulled) {
                return PhasesColdPull::cleanup;
            }

            // Let the user remove the pulled filament without releasing XYZ.
            disable_e_steppers();
            thermalManager.disable_hotend();
    #endif

            // mark filament unloaded
            config_store().set_filament_type(virtual_tool, FilamentType::none);
            filament_gcodes::M70X_process_user_response(PreheatStatus::Result::DoneNoFilament, virtual_tool);
        }
        return PhasesColdPull::manual_pull;
    }

    PhasesColdPull manual_pull() {
        // Keep it heated to HOTEND_END_TEMP to make manual pull easier

        was_success = true;

        switch (wait_for_response(PhasesColdPull::manual_pull)) {
        case Response::Continue:
            break;
        default:
            bsod_unreachable();
        }

    #if HAS_INDX()
        return PhasesColdPull::dock;
    #else
        return PhasesColdPull::cleanup;
    #endif
    }

    #if HAS_INDX()
    PhasesColdPull dock() {
        // The heater goes off before the wipe; residual heat carries the few seconds of wipe and dock.
        heat_up(INDX_WIPE_TEMP, PhasesColdPull::dock);
        thermalManager.disable_hotend();

        // As at the end of a print: wipe, park, dock. Stop at the first step that fails, and don't report
        // success while the tool may still be attached.
        if (!nozzle_cleaner::load_and_execute(nozzle_cleaner::Sequence::clean)
            || !mapi::park(mapi::get_parking_position(mapi::ParkPosition::park).without_z_move())
            || !tool_change(NoTool {}, tool_return_t::no_return)) {
            was_success = false;
        }
        return PhasesColdPull::cleanup;
    }
    #endif

    PhasesColdPull pull_done() {
        switch (wait_for_response(PhasesColdPull::pull_done)) {
        case Response::Finish:
            break;
        default:
            bsod_unreachable();
        }

        return PhasesColdPull::finish;
    }

    PhasesColdPull get_next_phase(const PhasesColdPull phase) {
        switch (phase) {
        case PhasesColdPull::introduction:
            return info();
    #if HAS_TOOLCHANGER()
        case PhasesColdPull::select_tool:
            return select_tool();
        case PhasesColdPull::pick_tool:
            return pick_tool();
    #endif
    #if HAS_MMU2()
        case PhasesColdPull::stop_mmu:
            return stop_mmu();
    #endif
    #if HAS_TOOLCHANGER() || HAS_MMU2()
        case PhasesColdPull::unload_ptfe:
            return unload_ptfe();
        case PhasesColdPull::load_ptfe:
            return load_ptfe();
    #endif
        case PhasesColdPull::prepare_filament:
            return prepare_filament();
    #if HAS_INDX()
        case PhasesColdPull::flush:
            return flush();
        case PhasesColdPull::purge_ready:
            return purge_ready();
        case PhasesColdPull::purge:
            return purge();
        case PhasesColdPull::tip_check:
            return tip_check();
        case PhasesColdPull::pack:
            return pack();
        case PhasesColdPull::pull_ready:
            switch (wait_for_response(PhasesColdPull::pull_ready)) {
            case Response::Continue:
                return PhasesColdPull::automatic_pull;
            case Response::Abort:
                return PhasesColdPull::cleanup;
            default:
                bsod_unreachable();
            }
    #endif
        case PhasesColdPull::blank_unload:
            return blank_unload();
        case PhasesColdPull::blank_load:
            return blank_load();
        case PhasesColdPull::cool_down:
            return cool_down();
    #if HAS_INDX()
        case PhasesColdPull::cooling_dwell:
            return cooling_dwell();
    #endif
    #if HAS_AUTO_RETRACT()
        case PhasesColdPull::deretract:
            return deretract();
    #endif
        case PhasesColdPull::heat_up:
    #if HAS_INDX()
            return heat_up_for_pull(PhasesColdPull::heat_up) ? PhasesColdPull::pull_ready : PhasesColdPull::cleanup;
    #else
            return heat_up(HOTEND_PULL_TEMP, PhasesColdPull::heat_up) ? PhasesColdPull::automatic_pull : PhasesColdPull::cleanup;
    #endif
        case PhasesColdPull::automatic_pull:
            return automatic_pull();
        case PhasesColdPull::manual_pull:
            return manual_pull();
    #if HAS_INDX()
        case PhasesColdPull::dock:
            return dock();
    #endif
        case PhasesColdPull::cleanup:
    #if HAS_MMU2()
            return cleanup();
    #elif HAS_INDX()
            return was_success ? PhasesColdPull::pull_done : PhasesColdPull::finish;
    #else
            return PhasesColdPull::finish;
    #endif
        case PhasesColdPull::pull_done:
            return pull_done();
        case PhasesColdPull::finish:
            break;
        }
        bsod_unreachable();
    }

} // namespace

/** \addtogroup G-Codes
 * @{
 */

/**
 *### M1702: Cold pull
 *
 * Internal GCode
 *
 *#### Usage
 *
 *    M1702
 *
 */

void M1702() {
    // Prevent filament autoload during whole ColdPull workflow.
    FS_AutoloadAutolock lock;
    // Also suppress runout/autoload events. Unlike set_enabled_global(false),
    // this is temporary and does not rewrite the user's persistent setting.
    FS_EventAutolock event_lock;

    #if HAS_MMU2()
    was_mmu_enabled = MMU2::mmu2.Enabled();
    #endif

    was_success = false;

    PhasesColdPull phase = PhasesColdPull::introduction;
    marlin_server::FSM_Holder holder { PhasesColdPull::introduction };
    do {
        phase = get_next_phase(phase);
        marlin_server::fsm_change(phase, {});
    } while (phase != PhasesColdPull::finish);

    // Clean up
    thermalManager.zero_fan_speeds();
    thermalManager.disable_all_heaters();
}

#else

void M1702() {}

#endif

} // namespace PrusaGcodeSuite

/** @}*/
