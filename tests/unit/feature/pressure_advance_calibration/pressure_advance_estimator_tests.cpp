#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <feature/pressure_advance_calibration/pressure_advance_estimator.hpp>

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using namespace pressure_advance_calibration;
using namespace Catch::Matchers;

namespace {

/// Extruder pushing against a nozzle whose pressure follows it as a first order low pass.
/// Pressure advance equal to the time constant makes the force follow the commanded flow exactly.
struct Simulation {
    double time_constant = 0.045;

    /// [g per mm/s] Negative if the load cell is mounted the other way
    double force_per_feedrate = 80;
    double force_offset = -350;
    double force_noise = 2;

    /// [s] How old the force in a sample is by the time of its timestamp
    double sensor_delay = 0;

    double sample_rate = 366;

    /// Samples in <first, second> seconds since the start of the simulation get lost
    std::pair<double, double> lost_samples = { 0, 0 };

    double e_acceleration = 120;
    double smooth_time = 0.04;
    double steps_per_mm = 550;
    double e_start = 123.4;
    uint32_t start_time_us = 1'000'000;

    /// The extruder goes this many times faster than the pattern says
    double feedrate_factor = 1;

    /// How much of the slower of the two feedrates the extruder keeps at the junction of two legs
    double junction_feedrate_factor = 1;

    uint8_t cycles = 4;
    double slow_feedrate = 0.5;
    double fast_feedrate = 2.0;

    uint32_t noise_seed = 1;

    Pattern pattern() const {
        return plan_pattern({
            .slow_feedrate = static_cast<float>(slow_feedrate),
            .fast_feedrate = static_cast<float>(fast_feedrate),
            .acceleration = static_cast<float>(e_acceleration),
            .smooth_time = static_cast<float>(smooth_time),
            .cycles = cycles,
        });
    }

    StepResponseAnalyzer::Config analyzer_config(double pressure_advance) const {
        return {
            .pattern = pattern(),
            .e_start = static_cast<float>(e_start),
            .pressure_advance = static_cast<float>(pressure_advance),
            .min_force_step = 20,
        };
    }

    static constexpr double dt = 0.0001;
    static constexpr double rest_time = 0.3;

    /// Commanded extruder feedrate over time, the way the planner executes the pattern:
    /// the junctions are taken at the slower of the two feedrates at most, the rest is accelerated within the legs.
    std::vector<double> commanded_feedrate() const {
        const Pattern pattern = this->pattern();
        std::vector<double> result;
        const auto rest = [&] {
            result.insert(result.end(), static_cast<size_t>(rest_time / dt), 0.0);
        };

        rest();
        for (uint8_t leg = 0; leg < pattern.leg_count(); leg++) {
            const double cruise = pattern.leg_feedrate(leg) * feedrate_factor;
            const double entry = leg == 0 ? 0 : junction_feedrate_factor * std::min(cruise, pattern.leg_feedrate(leg - 1) * feedrate_factor);
            const double exit = leg + 1 == pattern.leg_count() ? 0 : junction_feedrate_factor * std::min(cruise, pattern.leg_feedrate(leg + 1) * feedrate_factor);
            const double length = pattern.leg_length(leg);
            const double braking_distance = (cruise * cruise - exit * exit) / (2 * e_acceleration);

            double feedrate = entry;
            for (double done = 0; done < length;) {
                if (length - done <= braking_distance) {
                    feedrate = std::max(feedrate - e_acceleration * dt, std::max(exit, 0.01));
                } else {
                    feedrate = std::min(feedrate + e_acceleration * dt, cruise);
                }
                result.push_back(feedrate);
                done += feedrate * dt;
            }
        }
        rest();
        return result;
    }

    /// Position of the extruder motor: the commanded one with the pressure advance lead, smoothed the way the firmware does it
    std::vector<double> motor_position(const std::vector<double> &feedrate, double pressure_advance) const {
        std::vector<double> advanced(feedrate.size());
        double position = 0;
        for (size_t i = 0; i < feedrate.size(); i++) {
            advanced[i] = position + pressure_advance * feedrate[i];
            position += feedrate[i] * dt;
        }
        if (pressure_advance <= 0) {
            return advanced;
        }

        const int half_window = static_cast<int>(smooth_time / 2 / dt);
        std::vector<double> result(feedrate.size());
        for (int i = 0; i < static_cast<int>(feedrate.size()); i++) {
            double sum = 0;
            double weights = 0;
            for (int j = -half_window; j <= half_window; j++) {
                const int index = std::clamp(i + j, 0, static_cast<int>(feedrate.size()) - 1);
                const double weight = half_window + 1 - std::abs(j);
                sum += weight * advanced[index];
                weights += weight;
            }
            result[i] = sum / weights;
        }
        return result;
    }

    std::vector<Sample> samples(double pressure_advance) const {
        const auto motor = motor_position(commanded_feedrate(), pressure_advance);

        std::vector<double> force(motor.size());
        double pressure = 0;
        for (size_t i = 1; i < motor.size(); i++) {
            const double motor_feedrate = (motor[i] - motor[i - 1]) / dt;
            pressure += (motor_feedrate - pressure) * (1 - std::exp(-dt / time_constant));
            force[i] = force_offset + force_per_feedrate * pressure;
        }

        std::mt19937 generator(noise_seed + static_cast<uint32_t>(pressure_advance * 1e6));
        std::normal_distribution<double> noise(0, 1);

        std::vector<Sample> result;
        for (double time = 0;; time += 1 / sample_rate) {
            const size_t index = static_cast<size_t>(time / dt);
            if (index >= motor.size()) {
                break;
            }
            const size_t force_index = static_cast<size_t>(std::max(time - sensor_delay, 0.0) / dt);
            if (time >= lost_samples.first && time < lost_samples.second) {
                continue;
            }
            result.push_back({
                .time_us = start_time_us + static_cast<uint32_t>(time * 1e6),
                .force = static_cast<float>(force[force_index] + force_noise * noise(generator)),
                .e_position = static_cast<float>(e_start + std::floor(motor[index] * steps_per_mm) / steps_per_mm),
            });
        }
        return result;
    }

    using Response = std::expected<StepResponse, StepResponseError>;

    Response measure(const StepResponseAnalyzer::Config &config) const {
        StepResponseAnalyzer analyzer(config);
        for (const Sample &sample : samples(static_cast<double>(config.pressure_advance))) {
            analyzer.process(sample);
        }
        return analyzer.result();
    }

    Response measure(double pressure_advance) const {
        return measure(analyzer_config(pressure_advance));
    }

    std::expected<float, SearchError> calibrate(const PressureAdvanceSearch::Config &config = {}) const {
        PressureAdvanceSearch search(config);
        while (const auto pressure_advance = search.next()) {
            CAPTURE(*pressure_advance);
            const auto response = measure(static_cast<double>(*pressure_advance));
            REQUIRE(response.has_value());
            search.add(*pressure_advance, response->lag);
        }
        return search.result();
    }
};

} // namespace

TEST_CASE("pressure_advance_calibration::plan_pattern", "[pressure_advance_calibration]") {
    const float acceleration = GENERATE(50.0f, 120.0f, 5000.0f);
    const float fast_feedrate = GENERATE(1.0f, 3.0f, 8.0f);
    CAPTURE(acceleration, fast_feedrate);

    const Pattern pattern = plan_pattern({
        .slow_feedrate = fast_feedrate / 4,
        .fast_feedrate = fast_feedrate,
        .acceleration = acceleration,
        .smooth_time = 0.04f,
        .cycles = 3,
    });

    CHECK(pattern.leg_count() == 7);
    CHECK(pattern.lead_in_length >= pattern.slow_length);
    CHECK_THAT(pattern.total_length(), WithinRel(pattern.lead_in_length + 3 * (pattern.slow_length + pattern.fast_length), 1e-5f));

    for (uint8_t leg = 0; leg < pattern.leg_count(); leg++) {
        CAPTURE(leg);
        const Plateau &plateau = pattern.leg_plateau(leg);
        const float feedrate = pattern.leg_feedrate(leg);
        const float braking_distance = feedrate * feedrate / (2 * acceleration);

        // The plateau ends before the extruder has to start braking, with the pressure advance smoothing out of the way
        CHECK(plateau.end >= braking_distance + feedrate * 0.02f);

        // ... takes long enough to get a decent number of samples
        CHECK((plateau.begin - plateau.end) / feedrate >= 0.05f);

        // ... and begins long after the extruder has got up to speed
        CHECK((pattern.leg_length(leg) - plateau.begin - braking_distance) / feedrate >= 0.15f);
    }
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer lag", "[pressure_advance_calibration]") {
    Simulation simulation;
    simulation.force_noise = 0;
    simulation.time_constant = GENERATE(0.02, 0.045, 0.07);

    // More or less pressure advance than the nozzle needs
    const double pressure_advance = simulation.time_constant * GENERATE(0.0, 0.5, 1.0, 1.5);
    CAPTURE(simulation.time_constant, pressure_advance);

    const auto response = simulation.measure(pressure_advance);
    REQUIRE(response.has_value());
    CHECK(response->transitions == 2 * simulation.cycles);

    // The lag is what the pressure advance does not make up for. The part of the response that
    // has not settled by the time the plateau is measured is missing, both from it and from the force step.
    const double expected_lag = simulation.time_constant - pressure_advance;
    const double unsettled = std::abs(expected_lag) / simulation.time_constant;
    CHECK_THAT(response->lag, WithinAbs(expected_lag, 0.002 + std::abs(expected_lag) * 0.15));
    CHECK_THAT(response->force_step, WithinRel(simulation.force_per_feedrate * (simulation.fast_feedrate - simulation.slow_feedrate), 0.02 + 0.1 * unsettled));
    CHECK(response->lag_deviation < 0.002f);
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer force polarity", "[pressure_advance_calibration]") {
    Simulation simulation;
    simulation.force_per_feedrate = -80;

    const auto response = simulation.measure(0);
    REQUIRE(response.has_value());
    CHECK(response->force_step < 0);
    CHECK_THAT(response->lag, WithinAbs(simulation.time_constant, 0.006));
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer sensor delay", "[pressure_advance_calibration]") {
    Simulation simulation;
    simulation.force_noise = 0;
    const auto reference = simulation.measure(0.02);
    REQUIRE(reference.has_value());

    simulation.sensor_delay = 0.006;
    const auto delayed = simulation.measure(0.02);
    REQUIRE(delayed.has_value());
    CHECK_THAT(delayed->lag - reference->lag, WithinAbs(0.006, 0.0005));

    auto config = simulation.analyzer_config(0.02);
    config.force_delay = 0.006f;
    const auto compensated = simulation.measure(config);
    REQUIRE(compensated.has_value());
    CHECK_THAT(compensated->lag, WithinAbs(reference->lag, 0.0005));
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer timestamp overflow", "[pressure_advance_calibration]") {
    Simulation simulation;
    const auto reference = simulation.measure(0.03);
    REQUIRE(reference.has_value());

    // The timestamps wrap around in the middle of the pattern
    simulation.start_time_us = UINT32_MAX - 1'500'000;
    const auto response = simulation.measure(0.03);
    REQUIRE(response.has_value());
    CHECK(response->transitions == reference->transitions);
    CHECK_THAT(response->lag, WithinAbs(reference->lag, 0.0005));
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer lost samples", "[pressure_advance_calibration]") {
    Simulation simulation;
    const double pattern_start = Simulation::rest_time;
    const double pattern_duration = simulation.pattern().duration();

    SECTION("A short outage only costs the flow changes it hits") {
        simulation.lost_samples = { pattern_start + pattern_duration / 2, pattern_start + pattern_duration / 2 + 0.3 };
        const auto response = simulation.measure(0.02);
        REQUIRE(response.has_value());
        CHECK(response->transitions < 2 * simulation.cycles);
        CHECK_THAT(response->lag, WithinAbs(simulation.time_constant - 0.02, 0.006));
    }

    SECTION("No result if most of the pattern is missing") {
        simulation.lost_samples = { pattern_start + 0.2 * pattern_duration, pattern_start + 0.9 * pattern_duration };
        const auto response = simulation.measure(0.02);
        REQUIRE(!response.has_value());
        CHECK(response.error() == StepResponseError::missing_samples);
    }
}

TEST_CASE("pressure_advance_calibration::StepResponseAnalyzer errors", "[pressure_advance_calibration]") {
    Simulation simulation;

    SECTION("The extruder does not move") {
        StepResponseAnalyzer analyzer(simulation.analyzer_config(0));
        for (uint32_t i = 0; i < 1000; i++) {
            analyzer.process({ .time_us = i * 2732, .force = -350, .e_position = static_cast<float>(simulation.e_start) });
        }
        CHECK(!analyzer.is_finished());
        const auto response = analyzer.result();
        REQUIRE(!response.has_value());
        CHECK(response.error() == StepResponseError::incomplete);
    }

    SECTION("The extruder moves at a different feedrate") {
        simulation.feedrate_factor = 0.5;
        const auto response = simulation.measure(0);
        REQUIRE(!response.has_value());
        CHECK(response.error() == StepResponseError::unexpected_motion);
    }

    SECTION("The force barely changes with the flow") {
        simulation.force_per_feedrate = 5;
        const auto response = simulation.measure(0);
        REQUIRE(!response.has_value());
        CHECK(response.error() == StepResponseError::weak_signal);
    }

    SECTION("The force has nothing to do with the flow") {
        simulation.force_per_feedrate = 0;
        simulation.force_noise = 40;
        const auto response = simulation.measure(0);
        REQUIRE(!response.has_value());
        CHECK(response.error() == StepResponseError::weak_signal);
    }

    SECTION("Samples that cannot be used are skipped") {
        const auto reference = simulation.measure(0);
        REQUIRE(reference.has_value());

        StepResponseAnalyzer analyzer(simulation.analyzer_config(0));
        for (const Sample &sample : simulation.samples(0)) {
            analyzer.process(sample);
            // The same sample again, and one with the position unknown
            analyzer.process(sample);
            analyzer.process({ .time_us = sample.time_us + 100, .force = 1e6f, .e_position = NAN });
        }
        const auto response = analyzer.result();
        REQUIRE(response.has_value());
        CHECK(response->lag == reference->lag);
    }
}

TEST_CASE("pressure_advance_calibration::PressureAdvanceSearch", "[pressure_advance_calibration]") {
    Simulation simulation;
    simulation.time_constant = GENERATE(0.012, 0.03, 0.045, 0.07, 0.11, 0.16);
    simulation.fast_feedrate = GENERATE(1.4, 2.0, 3.5);
    simulation.slow_feedrate = simulation.fast_feedrate / 4;
    simulation.noise_seed = GENERATE(1u, 2u, 3u);
    CAPTURE(simulation.time_constant, simulation.fast_feedrate, simulation.noise_seed);

    const auto result = simulation.calibrate();
    REQUIRE(result.has_value());
    CHECK_THAT(*result, WithinAbs(simulation.time_constant, 0.002 + 0.03 * simulation.time_constant));
}

TEST_CASE("pressure_advance_calibration::PressureAdvanceSearch junction feedrate", "[pressure_advance_calibration]") {
    // However much the planner slows down between the legs, down to stopping there
    Simulation simulation;
    simulation.junction_feedrate_factor = GENERATE(0.0, 0.5, 1.0);
    simulation.e_acceleration = GENERATE(40.0, 120.0, 1000.0);
    simulation.fast_feedrate = GENERATE(2.0, 6.0);
    simulation.slow_feedrate = simulation.fast_feedrate / 4;
    CAPTURE(simulation.junction_feedrate_factor, simulation.e_acceleration, simulation.fast_feedrate);

    const auto result = simulation.calibrate();
    REQUIRE(result.has_value());
    CHECK_THAT(*result, WithinAbs(simulation.time_constant, 0.003));
}

TEST_CASE("pressure_advance_calibration::PressureAdvanceSearch nozzle without delay", "[pressure_advance_calibration]") {
    Simulation simulation;
    simulation.time_constant = 0.001;

    const auto result = simulation.calibrate();
    REQUIRE(result.has_value());
    CHECK_THAT(*result, WithinAbs(0, 0.003));
}

TEST_CASE("pressure_advance_calibration::PressureAdvanceSearch out of range", "[pressure_advance_calibration]") {
    Simulation simulation;
    simulation.time_constant = 0.09;

    const auto result = simulation.calibrate({ .max_pressure_advance = 0.05f });
    REQUIRE(!result.has_value());
    CHECK(result.error() == SearchError::out_of_range);
}

TEST_CASE("pressure_advance_calibration::PressureAdvanceSearch steps", "[pressure_advance_calibration]") {
    // Lag of a nozzle that needs 0.05 s of pressure advance
    const auto lag = [](float pressure_advance) {
        return 0.9f * (0.05f - pressure_advance);
    };

    SECTION("Measures as many step responses as asked for") {
        const uint8_t steps = GENERATE(3, 4, 6);
        PressureAdvanceSearch search({ .steps = steps });
        uint8_t count = 0;
        while (const auto pressure_advance = search.next()) {
            CHECK(*pressure_advance >= 0);
            CHECK(*pressure_advance <= 0.2f);
            search.add(*pressure_advance, lag(*pressure_advance));
            count++;
        }
        CHECK(count == steps);
        const auto result = search.result();
        REQUIRE(result.has_value());
        CHECK_THAT(*result, WithinAbs(0.05, 1e-4));
    }

    SECTION("Starts without pressure advance and goes on with what the lag suggests") {
        PressureAdvanceSearch search({});
        REQUIRE(search.next() == 0.0f);
        search.add(0, lag(0));
        REQUIRE(search.next().has_value());
        CHECK_THAT(*search.next(), WithinAbs(lag(0), 1e-6));
        search.add(*search.next(), lag(*search.next()));
        REQUIRE(search.next().has_value());
        CHECK_THAT(*search.next(), WithinAbs(0.05, 1e-4));
    }

    SECTION("No result before all the step responses are there") {
        PressureAdvanceSearch search({});
        search.add(0, lag(0));
        search.add(0.04f, lag(0.04f));
        const auto result = search.result();
        REQUIRE(!result.has_value());
        CHECK(result.error() == SearchError::not_enough_data);
    }

    SECTION("The lag has to shrink with the pressure advance") {
        PressureAdvanceSearch search({});
        while (const auto pressure_advance = search.next()) {
            // As if the pressure advance had no effect on the extruder
            search.add(*pressure_advance, 0.03f);
        }
        const auto result = search.result();
        REQUIRE(!result.has_value());
        CHECK(result.error() == SearchError::inconsistent);
    }

    SECTION("The step responses have to agree with each other") {
        PressureAdvanceSearch search({});
        search.add(0, lag(0));
        search.add(0.04f, lag(0.04f));
        search.add(0.05f, lag(0.05f) + 0.008f);
        search.add(0.05f, lag(0.05f) - 0.008f);
        const auto result = search.result();
        REQUIRE(!result.has_value());
        CHECK(result.error() == SearchError::noisy);
    }
}
