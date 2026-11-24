/// @file
/// @brief Hardware independent math of the pressure advance calibration
///
/// The nozzle pressure follows the extruder with a delay. Pressure advance makes the extruder lead
/// by the same time, so the right value is the one with which the load cell force changes exactly
/// when the commanded flow does. The calibration extrudes a pattern alternating between two
/// feedrates, measures the remaining delay with several pressure advance values and looks for
/// the value where the delay is zero.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace pressure_advance_calibration {

/// Load cell reading paired with the extruder motor position at the same instant
struct Sample {
    uint32_t time_us;

    /// [g] Neither the offset nor the sign matters
    float force;

    /// [mm] Position of the extruder motor, so with the pressure advance lead included
    float e_position;
};

/// Part of a leg with a steady flow, given as filament distances to the end of the leg
struct Plateau {
    float begin; ///< [mm]
    float end; ///< [mm] Smaller than begin
};

/// Extrusion alternating between a slow and a fast feedrate.
/// A slow lead-in leg comes first, followed by `cycles` pairs of a fast and a slow leg.
struct Pattern {
    float slow_feedrate; ///< [mm/s of filament]
    float fast_feedrate; ///< [mm/s of filament]
    float lead_in_length; ///< [mm of filament]
    float slow_length; ///< [mm of filament]
    float fast_length; ///< [mm of filament]
    Plateau slow_plateau;
    Plateau fast_plateau;
    uint8_t cycles;

    constexpr uint8_t leg_count() const {
        return 2 * cycles + 1;
    }

    constexpr bool is_fast(uint8_t leg) const {
        return leg % 2 == 1;
    }

    constexpr float leg_feedrate(uint8_t leg) const {
        return is_fast(leg) ? fast_feedrate : slow_feedrate;
    }

    constexpr float leg_length(uint8_t leg) const {
        if (leg == 0) {
            return lead_in_length;
        }
        return is_fast(leg) ? fast_length : slow_length;
    }

    constexpr const Plateau &leg_plateau(uint8_t leg) const {
        return is_fast(leg) ? fast_plateau : slow_plateau;
    }

    /// [mm of filament]
    constexpr float total_length() const {
        return lead_in_length + cycles * (slow_length + fast_length);
    }

    /// [s] Without the time spent accelerating
    constexpr float duration() const {
        return (lead_in_length + cycles * slow_length) / slow_feedrate + cycles * fast_length / fast_feedrate;
    }
};

struct PatternParams {
    float slow_feedrate; ///< [mm/s of filament]
    float fast_feedrate; ///< [mm/s of filament]

    /// [mm/s^2] The slowest the extruder may accelerate between the two feedrates
    float acceleration;

    /// [s] Pressure advance smooth time. The motor deviates from the commanded motion
    /// for half of it on each side of a feedrate change.
    float smooth_time;

    uint8_t cycles;
};

/// Lays the pattern out so that the flow is steady in the plateaus even with the slowest acceleration
Pattern plan_pattern(const PatternParams &params);

/// @returns how many cycles a pattern with these feedrates gets within `length` of filament [mm],
/// but `min_cycles` at the least and `max_cycles` at the most. `params.cycles` is not used.
/// The faster the flow, the more filament a cycle takes, and the fewer of them the lag takes to show.
uint8_t plan_cycles(const PatternParams &params, float length, uint8_t min_cycles, uint8_t max_cycles);

/// How the force responds to the flow changes of one pattern
struct StepResponse {
    /// [s] How much later than the commanded flow the force changes. Negative if it changes sooner.
    float lag;

    /// [s] The lag of the flow changes to the fast feedrate alone, and of those to the slow one.
    /// `lag` is the mean of the two.
    float rising_lag;
    float falling_lag;

    /// [s] Standard deviation of the lag of the individual flow changes
    float lag_deviation;

    /// [g] Force difference between the fast and the slow flow
    float force_step;

    /// [g] Force with the slow flow, offset included
    float slow_force;

    /// Number of flow changes the response is computed from
    uint8_t transitions;
};

enum class StepResponseError : uint8_t {
    /// The extruder has not gone through the whole pattern
    incomplete,

    /// The extruder moved differently than the pattern says
    unexpected_motion,

    /// The force changes too little with the flow
    weak_signal,

    /// Samples are missing around the flow changes
    missing_samples,
};

/// Course of the force around the flow changes of a pattern, averaged over the flow changes of
/// each direction. The calibration does not need it, it is for looking into what the nozzle pressure does.
class ResponseCurve {
public:
    /// [s] Time between two points of the curve
    static constexpr float resolution = 0.01f;

    /// [s] Where the curve begins, relative to the commanded flow change
    static constexpr float begin = -0.05f;

    /// Number of points, enough to reach the end of the plateau the force settles in
    static constexpr size_t size = 45;

    enum class Direction : uint8_t {
        rising,
        falling,
    };

    /// Forgets the curves, keeps the samples
    void clear();

    /// To be called with every sample, ordered by time. Only the latest ones are kept.
    void add_sample(uint32_t time_us, float force);

    /// Adds the samples around a flow change to the curve of its direction
    /// @param time_us when the flow has been commanded to change
    /// @param force_before force before the flow change
    /// @param force_step how much the force has changed by the time it settled
    void add_flow_change(Direction direction, uint32_t time_us, float force_before, float force_step);

    /// @returns the force at a point of the curve, as the part of the way from the force before the
    /// flow change (0) to the settled one (1). Nothing if no sample has fallen there.
    std::optional<float> at(Direction direction, size_t index) const;

private:
    struct StoredSample {
        uint32_t time_us;
        float force;
    };

    struct Point {
        float sum = 0;
        uint16_t count = 0;
    };

    /// Half a second of samples, which is what a curve spans
    std::array<StoredSample, 192> samples_;
    uint8_t sample_count_ = 0;
    uint8_t next_sample_ = 0;

    std::array<std::array<Point, size>, 2> points_;
};

/// Evaluates the step response from samples as they come, without storing them
class StepResponseAnalyzer {
public:
    struct Config {
        Pattern pattern;

        /// [mm] Extruder motor position at the start of the pattern
        float e_start;

        /// [s] Pressure advance in effect during the pattern
        float pressure_advance;

        /// [g] Flow changes with a smaller force difference are not evaluated
        float min_force_step;

        /// [s] How old the force in a sample is by the time of its timestamp
        float force_delay = 0;

        /// Gets the course of the force around the flow changes, if set. Has to outlive the analyzer.
        ResponseCurve *curve = nullptr;
    };

    /// Samples further apart than this around a flow change make it unusable
    static constexpr float max_sample_gap_s = 0.02f;

    /// A plateau with fewer samples is not trusted
    static constexpr uint16_t min_plateau_samples = 8;

    /// Tolerated relative difference between the commanded and the measured feedrate
    static constexpr float feedrate_tolerance = 0.15f;

    explicit StepResponseAnalyzer(const Config &config);

    /// Samples have to come ordered by time, starting before the extruder moves
    void process(const Sample &sample);

    /// @returns whether the extruder has gone through the whole pattern
    bool is_finished() const {
        return finished_;
    }

    std::expected<StepResponse, StepResponseError> result() const;

private:
    /// Sums for the means and the line fit of one plateau
    struct PlateauSums {
        uint32_t origin_us = 0;
        uint16_t count = 0;
        double t = 0;
        double tt = 0;
        double e = 0;
        double te = 0;
        double force = 0;
    };

    /// Mean force and the extruder position as a line in time
    struct PlateauFit {
        uint32_t origin_us;
        double force;
        double e_at_origin;
        double feedrate;

        double e_at(uint32_t time_us) const;
    };

    /// Force integrated between two plateaus
    struct Window {
        enum class State : uint8_t {
            idle,
            open,
            complete,
            unusable,
        };
        State state = State::idle;
        uint32_t begin_us = 0;
        uint32_t end_us = 0;
        double integral = 0;
    };

    struct LagStats {
        uint8_t count = 0;
        double lag = 0;
        double lag_squared = 0;
        double force_before = 0;
        double force_step = 0;

        void add(double lag_s, double force_before_g, double force_step_g);
        double mean_lag() const;
        double squared_deviations() const;
    };

    void close_plateau();
    void evaluate_transition(const PlateauFit &before, const PlateauFit &after, bool after_is_fast);
    static std::optional<PlateauFit> fit(const PlateauSums &sums);

    Config config_;

    std::optional<Sample> previous_;

    /// [mm] Commanded extruder position relative to the start of the pattern
    double nominal_e_ = 0;

    uint8_t leg_ = 0;

    /// [mm] End of the current leg, relative to the start of the pattern
    double leg_end_;

    PlateauSums plateau_;
    bool plateau_closed_ = false;
    std::optional<PlateauFit> previous_plateau_;
    Window window_;

    LagStats rising_;
    LagStats falling_;
    uint8_t unexpected_motion_count_ = 0;
    uint8_t weak_signal_count_ = 0;

    bool finished_ = false;
};

enum class SearchError : uint8_t {
    /// Fewer step responses than needed
    not_enough_data,

    /// The lag does not shrink with the pressure advance the way it should
    inconsistent,

    /// The step responses do not agree with each other well enough
    noisy,

    /// The lag is not zero anywhere within the allowed pressure advance range
    out_of_range,
};

/// Picks the pressure advance values to measure the step response with
/// and finds the one the lag is zero with.
class PressureAdvanceSearch {
public:
    static constexpr uint8_t max_steps = 8;
    static constexpr uint8_t min_steps = 3;

    struct Config {
        /// [s] The search stays within <0, max_pressure_advance>
        float max_pressure_advance = 0.2f;

        /// Number of step responses to measure
        uint8_t steps = 4;

        /// [s] Tolerated RMS distance of the lags from the line fitted through them,
        /// which is mostly the distance of those with the lag close to zero
        float max_residual = 0.004f;
    };

    /// Lag change per pressure advance change. One second of pressure advance removes one second
    /// of lag if the response is evaluated whole, and less the more of its tail is cut off.
    static constexpr float min_slope = -1.5f;
    static constexpr float max_slope = -0.3f;

    /// [s] The first non-zero pressure advance tried is at least this
    static constexpr float min_first_probe = 0.01f;

    /// [s] A zero lag this far below zero pressure advance still counts as no pressure advance needed
    static constexpr float negative_tolerance = 0.005f;

    /// [s] Step responses with a lag much larger than this barely count in the result
    static constexpr float lag_weight_scale = 0.01f;

    explicit PressureAdvanceSearch(const Config &config);

    /// @returns number of step responses the search measures
    uint8_t step_count() const {
        return config_.steps;
    }

    /// @returns pressure advance [s] to measure the step response with next, nullopt if there is enough of them
    std::optional<float> next() const;

    void add(float pressure_advance, float lag);

    /// @returns the pressure advance [s] the lag is zero with
    std::expected<float, SearchError> result() const;

    /// The lag as a line over the pressure advance
    struct Line {
        float lag_at_zero; ///< [s]
        float slope; ///< [s of lag per s of pressure advance]
        float residual; ///< [s] RMS distance of the weighted lags from the line

        bool is_slope_plausible() const {
            return slope >= min_slope && slope <= max_slope;
        }
    };

    /// @returns the line through the step responses added so far, the one result() comes from.
    /// Nothing if there are too few of them to fit one.
    std::optional<Line> fit() const;

private:
    struct Point {
        float pressure_advance;
        float lag;
    };

    Config config_;
    std::array<Point, max_steps> points_;
    uint8_t count_ = 0;
};

} // namespace pressure_advance_calibration
