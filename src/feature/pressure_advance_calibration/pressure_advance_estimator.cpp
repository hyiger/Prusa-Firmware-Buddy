/// @file
#include "pressure_advance_estimator.hpp"

#include <algorithm>
#include <cmath>

namespace pressure_advance_calibration {

namespace {

    double seconds_between(uint32_t from_us, uint32_t to_us) {
        // The subtraction survives the timestamp overflowing in between
        return static_cast<int32_t>(to_us - from_us) * 1e-6;
    }

} // namespace

Pattern plan_pattern(const PatternParams &params) {
    // Time the pressure gets to settle after a flow change before the plateau is measured.
    // The slow leg gets more: the pressure drops slower than it builds up.
    static constexpr float fast_settle_time = 0.17f;
    static constexpr float slow_settle_time = 0.30f;

    // Long enough to average the load cell noise out of the plateau
    static constexpr float plateau_time = 0.08f;

    // Each pattern starts with the extruder at rest and the nozzle pressure gone
    static constexpr float lead_in_time = 0.8f;

    const float guard_time = params.smooth_time / 2 + 0.01f;

    struct Leg {
        float length;
        Plateau plateau;
    };
    const auto plan_leg = [&](float feedrate, float settle_time) {
        // The junction with the neighboring leg might be taken at any lower feedrate,
        // at the worst the extruder accelerates from standstill and brakes back to it
        const float ramp_length = feedrate * feedrate / (2 * params.acceleration);
        const float plateau_end = ramp_length + feedrate * guard_time;
        const float plateau_begin = plateau_end + feedrate * plateau_time;
        return Leg {
            .length = ramp_length + feedrate * settle_time + plateau_begin,
            .plateau = { .begin = plateau_begin, .end = plateau_end },
        };
    };
    const Leg slow = plan_leg(params.slow_feedrate, slow_settle_time);
    const Leg fast = plan_leg(params.fast_feedrate, fast_settle_time);

    return Pattern {
        .slow_feedrate = params.slow_feedrate,
        .fast_feedrate = params.fast_feedrate,
        .lead_in_length = std::max(slow.length, params.slow_feedrate * lead_in_time),
        .slow_length = slow.length,
        .fast_length = fast.length,
        .slow_plateau = slow.plateau,
        .fast_plateau = fast.plateau,
        .cycles = params.cycles,
    };
}

StepResponseAnalyzer::StepResponseAnalyzer(const Config &config)
    : config_(config)
    , leg_end_(config.pattern.leg_length(0)) {}

double StepResponseAnalyzer::PlateauFit::e_at(uint32_t time_us) const {
    return e_at_origin + feedrate * seconds_between(origin_us, time_us);
}

void StepResponseAnalyzer::LagStats::add(double lag_s, double force_step_g) {
    count++;
    lag += lag_s;
    lag_squared += lag_s * lag_s;
    force_step += force_step_g;
}

double StepResponseAnalyzer::LagStats::mean_lag() const {
    return lag / count;
}

double StepResponseAnalyzer::LagStats::squared_deviations() const {
    return std::max(lag_squared - lag * lag / count, 0.0);
}

std::optional<StepResponseAnalyzer::PlateauFit> StepResponseAnalyzer::fit(const PlateauSums &sums) {
    if (sums.count < min_plateau_samples) {
        return std::nullopt;
    }
    const double n = sums.count;
    const double denominator = n * sums.tt - sums.t * sums.t;
    if (denominator <= 0) {
        return std::nullopt;
    }
    const double feedrate = (n * sums.te - sums.t * sums.e) / denominator;
    return PlateauFit {
        .origin_us = sums.origin_us,
        .force = sums.force / n,
        .e_at_origin = (sums.e - feedrate * sums.t) / n,
        .feedrate = feedrate,
    };
}

void StepResponseAnalyzer::evaluate_transition(const PlateauFit &before, const PlateauFit &after, bool after_is_fast) {
    const Pattern &pattern = config_.pattern;
    const double fast_feedrate = static_cast<double>(pattern.fast_feedrate);
    const double slow_feedrate = static_cast<double>(pattern.slow_feedrate);
    const auto is_expected = [](double measured, double commanded) {
        return std::abs(measured - commanded) <= commanded * static_cast<double>(feedrate_tolerance);
    };
    if (!is_expected(before.feedrate, after_is_fast ? slow_feedrate : fast_feedrate)
        || !is_expected(after.feedrate, after_is_fast ? fast_feedrate : slow_feedrate)) {
        unexpected_motion_count_++;
        return;
    }

    const double force_step = after.force - before.force;
    if (std::abs(force_step) < static_cast<double>(config_.min_force_step)) {
        weak_signal_count_++;
        return;
    }

    // Times are relative to the end of the window.
    // A force stepping at once at time T would integrate to (end - T) * force_step above the level
    // before, so the integral tells when the force changes, regardless of the shape of the change.
    const double window_duration = seconds_between(window_.begin_us, window_.end_us);
    const double force_time = -(window_.integral - before.force * window_duration) / force_step - static_cast<double>(config_.force_delay);

    // The same for the motor: its position lines of the two plateaus cross when its feedrate
    // would have to step at once to end up at the same place.
    const double motor_time = (after.e_at(window_.end_us) - before.e_at(window_.end_us)) / (before.feedrate - after.feedrate);

    // Pressure advance makes the motor lead the commanded motion by the pressure advance time
    const double command_time = motor_time + static_cast<double>(config_.pressure_advance);

    (after_is_fast ? rising_ : falling_).add(force_time - command_time, force_step);
}

void StepResponseAnalyzer::close_plateau() {
    const auto plateau_fit = fit(plateau_);
    if (plateau_fit && previous_plateau_ && window_.state == Window::State::complete) {
        evaluate_transition(*previous_plateau_, *plateau_fit, config_.pattern.is_fast(leg_));
    }
    previous_plateau_ = plateau_fit;
    plateau_ = {};
    plateau_closed_ = true;

    // The force is integrated from the last sample of this plateau to the first one of the next
    window_ = {};
    if (previous_plateau_) {
        window_.state = Window::State::open;
        window_.begin_us = previous_->time_us;
    }
}

void StepResponseAnalyzer::process(const Sample &sample) {
    if (finished_ || !std::isfinite(sample.force) || !std::isfinite(sample.e_position)) {
        return;
    }

    const double e = static_cast<double>(sample.e_position) - static_cast<double>(config_.e_start);
    if (!previous_) {
        previous_ = sample;
        nominal_e_ = e;
        return;
    }

    const double dt = seconds_between(previous_->time_us, sample.time_us);
    if (dt <= 0) {
        return;
    }

    // The motor is ahead of the commanded position by the commanded feedrate times the pressure
    // advance. That makes the commanded position a low pass of the motor position, with the
    // pressure advance as the time constant.
    nominal_e_ += (e - nominal_e_) * dt / (static_cast<double>(config_.pressure_advance) + dt);

    const uint8_t last_leg = config_.pattern.leg_count() - 1;
    while (nominal_e_ >= leg_end_) {
        if (!plateau_closed_) {
            close_plateau();
        }
        if (leg_ == last_leg) {
            finished_ = true;
            return;
        }
        leg_++;
        leg_end_ += static_cast<double>(config_.pattern.leg_length(leg_));
        plateau_closed_ = false;
    }

    const Plateau &plateau = config_.pattern.leg_plateau(leg_);
    const double to_leg_end = leg_end_ - nominal_e_;
    const bool is_past_plateau = !plateau_closed_ && to_leg_end < static_cast<double>(plateau.end);
    const bool is_in_plateau = !plateau_closed_ && !is_past_plateau && to_leg_end <= static_cast<double>(plateau.begin);

    if (is_past_plateau) {
        close_plateau();
    }

    if (window_.state == Window::State::open) {
        if (dt > static_cast<double>(max_sample_gap_s)) {
            window_.state = Window::State::unusable;
        } else {
            window_.integral += 0.5 * (static_cast<double>(previous_->force) + static_cast<double>(sample.force)) * dt;
        }
    }

    if (is_in_plateau) {
        if (plateau_.count == 0) {
            plateau_.origin_us = sample.time_us;
            if (window_.state == Window::State::open) {
                window_.state = Window::State::complete;
                window_.end_us = sample.time_us;
            }
        }
        const double t = seconds_between(plateau_.origin_us, sample.time_us);
        plateau_.count++;
        plateau_.t += t;
        plateau_.tt += t * t;
        plateau_.e += e;
        plateau_.te += t * e;
        plateau_.force += static_cast<double>(sample.force);
    }

    // Nothing comes after the plateau of the last leg
    if (is_past_plateau && leg_ == last_leg) {
        finished_ = true;
    }

    previous_ = sample;
}

std::expected<StepResponse, StepResponseError> StepResponseAnalyzer::result() const {
    if (!finished_) {
        return std::unexpected(StepResponseError::incomplete);
    }

    // The lag is the mean of the two directions, so that whatever shifts the rising and the falling
    // flow changes against each other cancels out. That takes at least one of each, and at least
    // half of all of them to be worth trusting.
    const uint8_t transitions = rising_.count + falling_.count;
    if (rising_.count == 0 || falling_.count == 0 || transitions < config_.pattern.cycles) {
        if (unexpected_motion_count_ > 0) {
            return std::unexpected(StepResponseError::unexpected_motion);
        } else if (weak_signal_count_ > 0) {
            return std::unexpected(StepResponseError::weak_signal);
        } else {
            return std::unexpected(StepResponseError::missing_samples);
        }
    }

    const double force_step = (rising_.force_step / rising_.count - falling_.force_step / falling_.count) / 2;
    if (std::abs(force_step) < static_cast<double>(config_.min_force_step)) {
        // The force goes the same way no matter which way the flow changes
        return std::unexpected(StepResponseError::weak_signal);
    }

    return StepResponse {
        .lag = static_cast<float>((rising_.mean_lag() + falling_.mean_lag()) / 2),
        .lag_deviation = static_cast<float>(std::sqrt((rising_.squared_deviations() + falling_.squared_deviations()) / transitions)),
        .force_step = static_cast<float>(force_step),
        .transitions = transitions,
    };
}

PressureAdvanceSearch::PressureAdvanceSearch(const Config &config)
    : config_(config) {
    config_.steps = std::clamp(config_.steps, min_steps, max_steps);
}

std::optional<PressureAdvanceSearch::Line> PressureAdvanceSearch::fit() const {
    if (count_ < 2) {
        return std::nullopt;
    }

    // The further from zero the lag is, the less of the response has settled by the time it is
    // measured and the more the lag falls short. So the closer to zero a point is, the more it counts.
    const auto weight = [](const Point &point) {
        return 1 / (point.lag * point.lag + lag_weight_scale * lag_weight_scale);
    };

    float sum_w = 0, sum_k = 0, sum_kk = 0, sum_lag = 0, sum_k_lag = 0;
    for (uint8_t i = 0; i < count_; i++) {
        const Point &point = points_[i];
        const float w = weight(point);
        sum_w += w;
        sum_k += w * point.pressure_advance;
        sum_kk += w * point.pressure_advance * point.pressure_advance;
        sum_lag += w * point.lag;
        sum_k_lag += w * point.pressure_advance * point.lag;
    }
    const float denominator = sum_w * sum_kk - sum_k * sum_k;
    if (denominator <= 0) {
        return std::nullopt;
    }
    Line line;
    line.slope = (sum_w * sum_k_lag - sum_k * sum_lag) / denominator;
    line.lag_at_zero = (sum_lag - line.slope * sum_k) / sum_w;

    float squared_residuals = 0;
    for (uint8_t i = 0; i < count_; i++) {
        const float residual = points_[i].lag - (line.lag_at_zero + line.slope * points_[i].pressure_advance);
        squared_residuals += weight(points_[i]) * residual * residual;
    }
    line.residual = std::sqrt(squared_residuals / sum_w);
    return line;
}

std::optional<float> PressureAdvanceSearch::next() const {
    if (count_ >= config_.steps) {
        return std::nullopt;
    }

    // Without pressure advance, the lag is the whole delay of the pressure
    if (count_ == 0) {
        return 0.0f;
    }

    // ... which makes it the first guess of the result
    if (count_ == 1) {
        return std::clamp(points_[0].lag, min_first_probe, config_.max_pressure_advance);
    }

    float largest_tried = 0;
    for (uint8_t i = 0; i < count_; i++) {
        largest_tried = std::max(largest_tried, points_[i].pressure_advance);
    }

    // Then keep measuring where the lag should be zero, to get the neighborhood of the result right
    const auto line = fit();
    if (line && line->is_slope_plausible()) {
        return std::clamp(-line->lag_at_zero / line->slope, 0.0f, config_.max_pressure_advance);
    }

    // Too close together to tell the slope from the noise
    return std::min(std::max(largest_tried, min_first_probe) * 2, config_.max_pressure_advance);
}

void PressureAdvanceSearch::add(float pressure_advance, float lag) {
    if (count_ < points_.size()) {
        points_[count_++] = { .pressure_advance = pressure_advance, .lag = lag };
    }
}

std::expected<float, SearchError> PressureAdvanceSearch::result() const {
    if (count_ < min_steps) {
        return std::unexpected(SearchError::not_enough_data);
    }
    const auto line = fit();
    if (!line || !line->is_slope_plausible()) {
        return std::unexpected(SearchError::inconsistent);
    }
    if (line->residual > config_.max_residual) {
        return std::unexpected(SearchError::noisy);
    }
    const float result = -line->lag_at_zero / line->slope;
    if (result < -negative_tolerance || result > config_.max_pressure_advance) {
        return std::unexpected(SearchError::out_of_range);
    }
    return std::max(result, 0.0f);
}

} // namespace pressure_advance_calibration
