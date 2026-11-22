/// @file
#include "pressure_advance_calibration.hpp"

#include "pressure_advance_estimator.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <utility>

#include <Marlin/src/Marlin.h>
#include <Marlin/src/module/motion.h>
#include <Marlin/src/module/planner.h>
#include <Marlin/src/module/temperature.h>
#include <config_store/store_instance.hpp>
#include <feature/pressure_advance/pressure_advance_config.hpp>
#include <feature/print_status_message/print_status_message_guard.hpp>
#include <feature/prusa/e-stall_detector.h>
#include <logging/log.hpp>
#include <mapi/motion.hpp>
#include <mapi/parking.hpp>
#include <metric.h>
#include <nozzle_cleaner.hpp>
#include <raii/scope_guard.hpp>
#include <timing.h>
#include <tool/hotend/hotend.hpp>
#include <utils/atomic_circular_queue.hpp>

#include <option/has_pressure_advance_calibration.h>

static_assert(HAS_PRESSURE_ADVANCE_CALIBRATION());

LOG_COMPONENT_DEF(PressureAdvanceCalib, logging::Severity::info);

// Step response measured with one pressure advance value
METRIC_DEF(metric_step_response, "pa_calib_step", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);
// Calibrated pressure advance of a tool
METRIC_DEF(metric_result, "pa_calib", METRIC_VALUE_CUSTOM, 0, METRIC_ENABLED);

namespace pressure_advance_calibration {

namespace {

    /// [mm/s of filament] Slower than this the force changes too little, faster than this the nozzle might not keep up
    constexpr float min_feedrate = 1.0f;
    constexpr float max_feedrate = 8.0f;

    constexpr float default_slow_feedrate_ratio = 0.25f;

    /// The two feedrates have to stay far enough apart for the force to tell them from each other
    constexpr float max_slow_feedrate_ratio = 0.6f;
    constexpr float min_slow_feedrate = 0.2f;

    constexpr uint8_t min_cycles = 2;
    constexpr uint8_t max_cycles = 8;

    /// [mm] The pressure advance only applies while the head moves, so the head goes back and forth by this much.
    /// Short enough to stay above the spot where the nozzle cleaner expects the blob.
    constexpr float wiggle = 0.4f;

    /// [mm of filament] Pushed through before the measurement, to get rid of what has oozed out
    constexpr float prime_length = 5.0f;

    /// [g] The force has to change at least this much between the two feedrates
    constexpr float min_force_step = 10.0f;

    /// [s] The largest pressure advance the calibration can end up with
    constexpr float max_pressure_advance = 0.2f;

    /// [s] How long to wait for the samples of a finished pattern to get here from the load cell
    constexpr int32_t sample_delivery_timeout_us = 300'000;

    /// [mm/s^2] The pattern is not planned for the extruder accelerating any slower than this
    constexpr float min_e_acceleration = 20.0f;

    std::array<std::optional<Request>, PhysicalToolIndex::count> requests;
    std::array<std::optional<float>, PhysicalToolIndex::count> results;

    /// Hands the samples over from where the load cell is read to the marlin task
    class SampleQueue {
    public:
        /// To be called from where the load cell is read
        void push(const Sample &sample) {
            if (is_open_.load(std::memory_order_relaxed) && !queue_.enqueue(sample)) {
                overflowed_.store(true, std::memory_order_relaxed);
            }
        }

        void open() {
            Sample sample;
            while (pop(sample)) {
            }
            overflowed_ = false;
            is_open_ = true;
        }

        void close() {
            is_open_ = false;
        }

        [[nodiscard]] bool pop(Sample &sample) {
            return queue_.dequeue(sample);
        }

        /// @returns whether samples got lost because nobody was taking them out fast enough
        bool has_overflowed() const {
            return overflowed_;
        }

    private:
        // A fifth of a second of samples, the marlin task normally comes around much sooner
        AtomicCircularQueue<Sample, uint8_t, 64> queue_;
        std::atomic<bool> is_open_ = false;
        std::atomic<bool> overflowed_ = false;
    };

    SampleQueue sample_queue;

    /// @returns the slowest the extruder might accelerate during the pattern [mm/s^2]
    float e_acceleration(float slow_feedrate) {
        const auto &settings = planner.settings;
        float result = static_cast<float>(settings.max_acceleration_mm_per_s2[E_AXIS]);
        if (settings.max_filament_acceleration_mm_per_s2 > 0) {
            result = std::min(result, settings.max_filament_acceleration_mm_per_s2);
        }
        // The print acceleration is the one of the head. The extruder gets as much of it as it
        // travels more than the head, which is the least in the slow legs. Those take about
        // half a second, so the extruder travels about half of the slow feedrate in them.
        result = std::min(result, settings.acceleration * slow_feedrate * 0.5f / wiggle);
        return std::max(result, min_e_acceleration);
    }

    const char *to_string(StepResponseError error) {
        switch (error) {
        case StepResponseError::incomplete:
            return "extruder did not finish the pattern";
        case StepResponseError::unexpected_motion:
            return "extruder did not follow the pattern";
        case StepResponseError::weak_signal:
            return "force does not follow the flow";
        case StepResponseError::missing_samples:
            return "load cell samples missing";
        }
        return "";
    }

    const char *to_string(SearchError error) {
        switch (error) {
        case SearchError::not_enough_data:
            return "not enough step responses";
        case SearchError::inconsistent:
            return "lag does not follow the pressure advance";
        case SearchError::noisy:
            return "step responses do not agree";
        case SearchError::out_of_range:
            return "out of range";
        }
        return "";
    }

    /// Extrudes the pattern with the given pressure advance and evaluates how the force responds.
    /// The head goes back and forth around center_y meanwhile.
    std::expected<StepResponse, Failure> measure_step_response(const Pattern &pattern, float pressure_advance, float smooth_time, float center_y) {
        planner.synchronize();
        if (planner.draining()) {
            return std::unexpected(Failure::aborted);
        }
        pressure_advance::set_axis_e_config({ .pressure_advance = pressure_advance, .smooth_time = smooth_time });

        StepResponseAnalyzer analyzer({
            .pattern = pattern,
            .e_start = planner.get_axis_position_mm(E_AXIS),
            .pressure_advance = pressure_advance,
            .min_force_step = min_force_step,
        });
        const auto analyze_samples = [&] {
            Sample sample;
            while (sample_queue.pop(sample)) {
                analyzer.process(sample);
            }
        };

        sample_queue.open();
        ScopeGuard close_queue = [] {
            sample_queue.close();
        };

        // The analyzer needs to see the extruder at rest first. And there is no point in extruding
        // anything if the load cell is not being read.
        {
            const uint32_t open_us = ticks_us();
            Sample sample;
            while (!sample_queue.pop(sample)) {
                if (planner.draining()) {
                    return std::unexpected(Failure::aborted);
                }
                if (ticks_diff(ticks_us(), open_us) > sample_delivery_timeout_us) {
                    log_error(PressureAdvanceCalib, "No load cell samples");
                    return std::unexpected(Failure::measurement_failed);
                }
                idle(true);
            }
            analyzer.process(sample);
        }

        MachinePosXYZE target = current_machine_position();
        for (uint8_t leg = 0; leg < pattern.leg_count(); leg++) {
            const float length = pattern.leg_length(leg);
            const float next_y = center_y + (target.y < center_y ? wiggle : -wiggle) / 2;
            const float distance = std::abs(next_y - target.y);
            target.y = next_y;
            target.e += length;

            // The feedrate of a move is the one of the head
            line_to_machine_pos(target, pattern.leg_feedrate(leg) * distance / length, { .ignore_e_factor = true });
            analyze_samples();
        }

        while (planner.processing() && !planner.draining()) {
            idle(true);
            analyze_samples();
        }
        if (planner.draining()) {
            return std::unexpected(Failure::aborted);
        }

        const uint32_t motion_end_us = ticks_us();
        while (!analyzer.is_finished() && ticks_diff(ticks_us(), motion_end_us) < sample_delivery_timeout_us) {
            idle(true);
            analyze_samples();
        }

        if (sample_queue.has_overflowed()) {
            log_warning(PressureAdvanceCalib, "Load cell samples lost");
        }

        const auto response = analyzer.result();
        if (!response) {
            log_error(PressureAdvanceCalib, "K=%.4f: %s", static_cast<double>(pressure_advance), to_string(response.error()));
            return std::unexpected(Failure::measurement_failed);
        }

        log_info(PressureAdvanceCalib, "K=%.4f: lag %.4f s (+-%.4f), force step %.1f g, %u transitions",
            static_cast<double>(pressure_advance), static_cast<double>(response->lag), static_cast<double>(response->lag_deviation),
            static_cast<double>(response->force_step), response->transitions);
        metric_record_custom(&metric_step_response, " k=%.4f,lag=%.4f,dev=%.4f,force=%.1f,n=%ui",
            static_cast<double>(pressure_advance), static_cast<double>(response->lag), static_cast<double>(response->lag_deviation),
            static_cast<double>(response->force_step), response->transitions);
        return *response;
    }

    /// Calibrates the pressure advance of the picked tool with its nozzle at the purge position
    std::expected<float, Failure> calibrate(PhysicalToolIndex tool, const Request &request) {
        if (PhysicalToolIndex::currently_selected_opt() != tool) {
            return std::unexpected(Failure::not_ready);
        }

        const float fast_feedrate = std::clamp(request.feedrate, min_feedrate, max_feedrate);
        const float slow_feedrate = std::clamp(request.slow_feedrate.value_or(fast_feedrate * default_slow_feedrate_ratio), min_slow_feedrate, fast_feedrate * max_slow_feedrate_ratio);
        PressureAdvanceSearch search({
            .max_pressure_advance = max_pressure_advance,
            .steps = request.steps,
        });

        PrintStatusMessageGuard status;
        uint8_t step = 0;
        const auto report_progress = [&] {
            status.update<PrintStatusMessage::Type::pressure_advance_calibrating>({
                .progress = { .current = static_cast<float>(step), .target = static_cast<float>(search.step_count()) },
                .tool = tool.to_raw(),
            });
        };
        report_progress();

        Hotend::for_tool(tool).set_nozzle_target_temp(request.nozzle_temperature);
        if (!thermalManager.wait_for_hotend(tool, { .no_wait_for_cooling = false }) || planner.draining()) {
            return std::unexpected(Failure::aborted);
        }
        if (thermalManager.tooColdToExtrude(tool)) {
            return std::unexpected(Failure::not_ready);
        }
        report_progress();

        // The pattern would look like a clogged nozzle clearing itself over and over
        BlockEStallDetection estall_blocker;

        const pressure_advance::Config original_config = pressure_advance::get_axis_e_config();
        ScopeGuard restore_config = [&] {
            // Unlike synchronize(), this also waits for the moves of a stopped print to be thrown away
            planner.wait_for_motion_finish();
            pressure_advance::set_axis_e_config(original_config);
        };

        // The first extrusion also takes back the retraction the tool has been parked with
        mapi::extruder_move(prime_length, fast_feedrate);

        const MachinePosXYZE origin = current_machine_position();
        ScopeGuard return_to_origin = [&] {
            MachinePosXYZE position = current_machine_position();
            position.y = origin.y;
            line_to_machine_pos(position, NOZZLE_PARK_XY_FEEDRATE);
        };

        const Pattern pattern = plan_pattern({
            .slow_feedrate = slow_feedrate,
            .fast_feedrate = fast_feedrate,
            .acceleration = e_acceleration(slow_feedrate),
            .smooth_time = original_config.smooth_time,
            .cycles = std::clamp(request.cycles, min_cycles, max_cycles),
        });
        log_info(PressureAdvanceCalib, "T%u: %i C, %.2f/%.2f mm/s, %u cycles, %.1f mm of filament per step",
            tool.to_raw(), request.nozzle_temperature, static_cast<double>(slow_feedrate), static_cast<double>(fast_feedrate),
            pattern.cycles, static_cast<double>(pattern.total_length()));

        while (const auto pressure_advance = search.next()) {
            const auto response = measure_step_response(pattern, *pressure_advance, original_config.smooth_time, origin.y);
            if (!response) {
                return std::unexpected(response.error());
            }
            search.add(*pressure_advance, response->lag);
            step++;
            report_progress();
        }

        const auto result = search.result();
        if (!result) {
            log_error(PressureAdvanceCalib, "T%u: %s", tool.to_raw(), to_string(result.error()));
            return std::unexpected(Failure::no_result);
        }
        return *result;
    }

    std::expected<float, Failure> calibrate_and_store(PhysicalToolIndex tool, const Request &request) {
        const auto result = calibrate(tool, request);
        results[tool.to_raw()] = result ? std::optional(*result) : std::nullopt;
        if (result) {
            log_info(PressureAdvanceCalib, "T%u: pressure advance %.4f", tool.to_raw(), static_cast<double>(*result));
            metric_record_custom(&metric_result, ",tool=%u k=%.4f", tool.to_raw(), static_cast<double>(*result));
        }
        return result;
    }

} // namespace

bool is_enabled() {
    return config_store().pressure_advance_calibration.get();
}

void request(PhysicalToolIndex tool, const Request &request) {
    requests[tool.to_raw()] = request;
}

void run_requested(PhysicalToolIndex tool) {
    const auto request = std::exchange(requests[tool.to_raw()], std::nullopt);
    if (request && is_enabled()) {
        // A failure is not worth stopping the print for, it carries on with the pressure advance of the slicer
        (void)calibrate_and_store(tool, *request);
    }
}

std::expected<float, Failure> run_now(const Request &request) {
    const auto tool = PhysicalToolIndex::currently_selected_opt();
    if (!tool || !axes_home_level.is_homed(X_AXIS, AxisHomeLevel::imprecise) || !axes_home_level.is_homed(Y_AXIS, AxisHomeLevel::imprecise)) {
        return std::unexpected(Failure::not_ready);
    }

    Hotend &hotend = Hotend::for_tool(*tool);
    const auto original_target_temp = hotend.nozzle_target_temp();
    ScopeGuard restore_target_temp = [&] {
        hotend.set_nozzle_target_temp(original_target_temp);
    };

    mapi::park(mapi::get_parking_position(mapi::ParkPosition::nozzle_cleaner_approach));

    // Ends with the nozzle at the purge position
    if (!nozzle_cleaner::load_and_execute(nozzle_cleaner::Sequence::eject_blob)) {
        return std::unexpected(Failure::aborted);
    }

    const auto result = calibrate_and_store(*tool, request);

    // Gets rid of the blob, retracts and wipes the nozzle
    if (!nozzle_cleaner::load_and_execute(nozzle_cleaner::Sequence::purge_clean)) {
        return std::unexpected(Failure::aborted);
    }
    mapi::park(mapi::get_parking_position(mapi::ParkPosition::nozzle_cleaner_exit));

    return result;
}

std::optional<float> result(PhysicalToolIndex tool) {
    return results[tool.to_raw()];
}

bool apply() {
    const auto tool = PhysicalToolIndex::currently_selected_opt();
    if (!tool || !is_enabled()) {
        return false;
    }
    const auto pressure_advance = result(*tool);
    if (!pressure_advance) {
        return false;
    }

    pressure_advance::Config config = pressure_advance::get_axis_e_config();
    if (config.pressure_advance != *pressure_advance) {
        config.pressure_advance = *pressure_advance;
        planner.synchronize();
        if (!planner.draining()) {
            pressure_advance::set_axis_e_config(config);
        }
    }
    return true;
}

void reset() {
    requests.fill(std::nullopt);
    results.fill(std::nullopt);
}

void process_sample(uint32_t time_us, float force, float e_position) {
    sample_queue.push({ .time_us = time_us, .force = force, .e_position = e_position });
}

} // namespace pressure_advance_calibration
