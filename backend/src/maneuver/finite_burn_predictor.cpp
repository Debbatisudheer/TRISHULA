#include "trishula/maneuver/finite_burn_predictor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kG0 = 9.80665;
constexpr double kEpsilon = 1e-12;
constexpr int kBisectionIterations = 30;


double periapsis_radius_m(const NavigationState& nav) {
    if (!(nav.orbit.semi_major_axis_meters > 0.0) || !(nav.orbit.eccentricity >= 0.0)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return nav.orbit.semi_major_axis_meters * (1.0 - nav.orbit.eccentricity);
}

TrueState estimated_as_true(const EstimatedState& estimated, double mass_kg) {
    TrueState state{};
    state.time_seconds = estimated.time_seconds;
    state.position_meters = estimated.position_meters;
    state.velocity_m_per_s = estimated.velocity_m_per_s;
    state.acceleration_m_per_s2 = estimated.acceleration_m_per_s2;
    state.attitude_body_to_inertial = estimated.attitude_body_to_inertial;
    state.angular_velocity_rad_s = estimated.angular_velocity_rad_s;
    state.mass_kg = mass_kg;
    return state;
}


}

FiniteBurnPredictor::FiniteBurnPredictor(double time_step_seconds)
    : step_seconds_(time_step_seconds) {
    if (time_step_seconds <= 0.0) {
        throw std::invalid_argument("Finite burn predictor step must be positive");
    }
}

NavigationState FiniteBurnPredictor::predict(
    const SimulationEngine& engine,
    const StateEstimator& estimator,
    double delta_v_m_per_s,
    int direction_sign) const {
    if (delta_v_m_per_s < 0.0 || direction_sign == 0) {
        throw std::invalid_argument("Finite burn prediction requires non-negative magnitude and non-zero direction");
    }

    const EstimatedState& estimated = estimator.state();
    TrueState shadow_state = estimated_as_true(
        estimated, engine.spacecraft().state().mass_kg);
    Spacecraft shadow_spacecraft(shadow_state, engine.spacecraft().inertia());
    SimulationEngine shadow(
        engine.primary_body(),
        shadow_spacecraft,
        step_seconds_,
        engine.main_engine(),
        engine.rcs_module(),
        engine.sensor_suite(),
        engine.perturbation_model().configuration());

    const double duration = duration_for_delta_v(engine, delta_v_m_per_s);
    double remaining = duration;
    while (remaining > kEpsilon) {
        const double dt = std::min(step_seconds_, remaining);
        const TrueState& current = shadow.spacecraft().state();
        const Vector3 radial = current.position_meters.normalized();
        const Vector3 tangential = current.velocity_m_per_s -
            radial * current.velocity_m_per_s.dot(radial);
        const Vector3 inertial_direction = tangential.normalized() * static_cast<double>(direction_sign);
        const Vector3 body_direction =
            current.attitude_body_to_inertial.inverse().rotate(inertial_direction);

        PropulsionCommand command{};
        command.main_engine_throttle = dt / step_seconds_;
        command.commanded_thrust_direction_body = body_direction;
        shadow.step(command);
        remaining -= dt;
    }

    EstimatedState final_state{};
    const TrueState& final_true = shadow.spacecraft().state();
    final_state.time_seconds = final_true.time_seconds;
    final_state.position_meters = final_true.position_meters;
    final_state.velocity_m_per_s = final_true.velocity_m_per_s;
    final_state.acceleration_m_per_s2 = final_true.acceleration_m_per_s2;
    final_state.attitude_body_to_inertial = final_true.attitude_body_to_inertial;
    final_state.angular_velocity_rad_s = final_true.angular_velocity_rad_s;
    final_state.altitude_meters = final_true.position_meters.magnitude() - engine.primary_body().radius();

    EarthCenteredNavigator navigator;
    return navigator.determine(final_state, engine.primary_body());
}

double FiniteBurnPredictor::duration_for_delta_v(
    const SimulationEngine& engine,
    double delta_v_m_per_s) const {
    if (delta_v_m_per_s <= 0.0) {
        return 0.0;
    }
    const double mass0 = engine.spacecraft().state().mass_kg;
    const double dry = engine.main_engine().dry_mass_kg();
    const double isp = engine.main_engine().specific_impulse_seconds();
    const double thrust = engine.main_engine().maximum_thrust_newtons();
    if (mass0 <= dry || isp <= 0.0 || thrust <= 0.0) {
        throw std::runtime_error("Finite burn prediction has invalid propulsion state");
    }
    const double final_mass = mass0 * std::exp(-delta_v_m_per_s / (isp * kG0));
    if (final_mass < dry - 1e-9) {
        throw std::runtime_error("Finite burn prediction exceeds available propellant");
    }
    const double mdot = thrust / (isp * kG0);
    return (mass0 - final_mass) / mdot;
}

PredictedBurnSolution FiniteBurnPredictor::solve_for_periapsis(
    const SimulationEngine& engine,
    const StateEstimator& estimator,
    double target_periapsis_radius_meters,
    double maximum_delta_v_m_per_s) const {
    PredictedBurnSolution result{};
    if (target_periapsis_radius_meters <= 0.0 || maximum_delta_v_m_per_s <= 0.0) {
        return result;
    }

    const auto current = EarthCenteredNavigator{}.determine(estimator.state(), engine.primary_body());
    const double current_periapsis = periapsis_radius_m(current);
    if (!std::isfinite(current_periapsis)) {
        return result;
    }

    const int direction = target_periapsis_radius_meters < current_periapsis ? -1 : 1;
    const double f0 = current_periapsis - target_periapsis_radius_meters;
    const auto at_max = predict(engine, estimator, maximum_delta_v_m_per_s, direction);
    const double fmax = periapsis_radius_m(at_max) - target_periapsis_radius_meters;

    double best_dv = 0.0;
    NavigationState best = current;
    double best_abs_error = std::abs(f0);

    if (std::isfinite(fmax) && std::abs(fmax) < best_abs_error) {
        best_dv = maximum_delta_v_m_per_s;
        best = at_max;
        best_abs_error = std::abs(fmax);
    }

    if (std::isfinite(fmax) && ((f0 <= 0.0 && fmax >= 0.0) || (f0 >= 0.0 && fmax <= 0.0))) {
        double low = 0.0;
        double high = maximum_delta_v_m_per_s;
        for (int i = 0; i < kBisectionIterations; ++i) {
            const double mid = 0.5 * (low + high);
            const auto candidate = predict(engine, estimator, mid, direction);
            const double fm = periapsis_radius_m(candidate) - target_periapsis_radius_meters;
            if (!std::isfinite(fm)) {
                break;
            }
            if (std::abs(fm) < best_abs_error) {
                best_abs_error = std::abs(fm);
                best_dv = mid;
                best = candidate;
            }
            if ((f0 <= 0.0 && fm <= 0.0) || (f0 >= 0.0 && fm >= 0.0)) {
                low = mid;
            } else {
                high = mid;
            }
        }
    }

    result.delta_v_m_per_s = static_cast<double>(direction) * best_dv;
    result.direction_sign = direction;
    result.duration_seconds = duration_for_delta_v(engine, best_dv);
    result.predicted_semi_major_axis_meters = best.orbit.semi_major_axis_meters;
    result.predicted_eccentricity = best.orbit.eccentricity;
    result.prediction_error_meters = best_abs_error;
    result.valid = std::isfinite(best_abs_error);
    return result;
}


int FiniteBurnPredictor::direction_for_target(
    const SimulationEngine& engine,
    const StateEstimator& estimator,
    double target_semi_major_axis_meters) const {
    const auto current = EarthCenteredNavigator{}.determine(estimator.state(), engine.primary_body());
    if (!std::isfinite(current.orbit.semi_major_axis_meters)) {
        return 0;
    }
    return target_semi_major_axis_meters >= current.orbit.semi_major_axis_meters ? 1 : -1;
}


namespace {

NavigationState propagate_to_next_periapsis(
    const SimulationEngine& engine,
    const StateEstimator& estimator,
    double delta_v_m_per_s,
    int direction_sign,
    double predictor_step_seconds,
    double maximum_coast_seconds) {
    const EstimatedState& estimated = estimator.state();
    TrueState shadow_state = estimated_as_true(
        estimated, engine.spacecraft().state().mass_kg);
    Spacecraft shadow_spacecraft(shadow_state, engine.spacecraft().inertia());
    SimulationEngine shadow(
        engine.primary_body(),
        shadow_spacecraft,
        predictor_step_seconds,
        engine.main_engine(),
        engine.rcs_module(),
        engine.sensor_suite(),
        engine.perturbation_model().configuration());

    const double burn_duration = delta_v_m_per_s > 0.0
        ? [&]() {
            const double mass0 = engine.spacecraft().state().mass_kg;
            const double dry = engine.main_engine().dry_mass_kg();
            const double isp = engine.main_engine().specific_impulse_seconds();
            const double thrust = engine.main_engine().maximum_thrust_newtons();
            const double final_mass = mass0 * std::exp(-delta_v_m_per_s / (isp * kG0));
            if (mass0 <= dry || isp <= 0.0 || thrust <= 0.0 || final_mass < dry - 1e-9) {
                throw std::runtime_error("Terminal coast prediction has invalid propulsion state");
            }
            const double mdot = thrust / (isp * kG0);
            return (mass0 - final_mass) / mdot;
        }()
        : 0.0;

    double remaining = burn_duration;
    while (remaining > kEpsilon) {
        const double dt = std::min(predictor_step_seconds, remaining);
        const TrueState& current = shadow.spacecraft().state();
        const Vector3 radial = current.position_meters.normalized();
        const Vector3 tangential = current.velocity_m_per_s -
            radial * current.velocity_m_per_s.dot(radial);
        const Vector3 inertial_direction = tangential.normalized() * static_cast<double>(direction_sign);
        const Vector3 body_direction =
            current.attitude_body_to_inertial.inverse().rotate(inertial_direction);
        PropulsionCommand command{};
        command.main_engine_throttle = dt / predictor_step_seconds;
        command.commanded_thrust_direction_body = body_direction;
        shadow.step(command);
        remaining -= dt;
    }

    EarthCenteredNavigator navigator;
    auto to_navigation = [&](const TrueState& state) {
        EstimatedState e{};
        e.time_seconds = state.time_seconds;
        e.position_meters = state.position_meters;
        e.velocity_m_per_s = state.velocity_m_per_s;
        e.acceleration_m_per_s2 = state.acceleration_m_per_s2;
        e.attitude_body_to_inertial = state.attitude_body_to_inertial;
        e.angular_velocity_rad_s = state.angular_velocity_rad_s;
        e.altitude_meters = state.position_meters.magnitude() - engine.primary_body().radius();
        return navigator.determine(e, engine.primary_body());
    };

    NavigationState previous = to_navigation(shadow.spacecraft().state());
    const double orbit_period = previous.orbit.semi_major_axis_meters > 0.0
        ? 2.0 * 3.14159265358979323846 * std::sqrt(
            std::pow(previous.orbit.semi_major_axis_meters, 3.0) /
            engine.primary_body().gravitational_parameter())
        : 0.0;
    const double coast_limit = maximum_coast_seconds > 0.0
        ? maximum_coast_seconds
        : std::max(2.0 * orbit_period, 7200.0);

    double elapsed = 0.0;
    const double minimum_coast = std::max(5.0 * predictor_step_seconds, 5.0);
    while (elapsed < coast_limit) {
        shadow.step({});
        elapsed += predictor_step_seconds;
        const auto current = to_navigation(shadow.spacecraft().state());
        if (elapsed >= minimum_coast &&
            previous.radial_velocity_m_per_s < 0.0 &&
            current.radial_velocity_m_per_s >= 0.0) {
            return current;
        }
        previous = current;
    }

    return to_navigation(shadow.spacecraft().state());
}

} // namespace

PredictedBurnSolution FiniteBurnPredictor::solve_for_periapsis_after_coast(
    const SimulationEngine& engine,
    const StateEstimator& estimator,
    double target_periapsis_radius_meters,
    double maximum_delta_v_m_per_s,
    double maximum_coast_seconds) const {
    PredictedBurnSolution result{};
    if (target_periapsis_radius_meters <= 0.0 || maximum_delta_v_m_per_s <= 0.0) {
        return result;
    }

    const auto current = EarthCenteredNavigator{}.determine(estimator.state(), engine.primary_body());
    const double current_periapsis = periapsis_radius_m(current);
    if (!std::isfinite(current_periapsis)) {
        return result;
    }

    const int direction = target_periapsis_radius_meters < current_periapsis ? -1 : 1;
    const double f0 = current_periapsis - target_periapsis_radius_meters;
    const double coast_step = std::max(step_seconds_, 5.0);
    const auto at_max = propagate_to_next_periapsis(
        engine, estimator, maximum_delta_v_m_per_s, direction,
        coast_step, maximum_coast_seconds);
    const double fmax = periapsis_radius_m(at_max) - target_periapsis_radius_meters;

    double best_dv = 0.0;
    NavigationState best = current;
    double best_abs_error = std::abs(f0);

    if (std::isfinite(fmax) && std::abs(fmax) < best_abs_error) {
        best_dv = maximum_delta_v_m_per_s;
        best = at_max;
        best_abs_error = std::abs(fmax);
    }

    if (std::isfinite(fmax) && ((f0 <= 0.0 && fmax >= 0.0) || (f0 >= 0.0 && fmax <= 0.0))) {
        double low = 0.0;
        double high = maximum_delta_v_m_per_s;
        constexpr int kCoastBisectionIterations = 18;
        for (int i = 0; i < kCoastBisectionIterations; ++i) {
            const double mid = 0.5 * (low + high);
            const auto candidate = propagate_to_next_periapsis(
                engine, estimator, mid, direction,
                coast_step, maximum_coast_seconds);
            const double fm = periapsis_radius_m(candidate) - target_periapsis_radius_meters;
            if (!std::isfinite(fm)) break;
            if (std::abs(fm) < best_abs_error) {
                best_abs_error = std::abs(fm);
                best_dv = mid;
                best = candidate;
            }
            if ((f0 <= 0.0 && fm <= 0.0) || (f0 >= 0.0 && fm >= 0.0)) {
                low = mid;
            } else {
                high = mid;
            }
        }
    }

    result.delta_v_m_per_s = static_cast<double>(direction) * best_dv;
    result.direction_sign = direction;
    result.duration_seconds = duration_for_delta_v(engine, best_dv);
    result.predicted_semi_major_axis_meters = best.orbit.semi_major_axis_meters;
    result.predicted_eccentricity = best.orbit.eccentricity;
    result.prediction_error_meters = best_abs_error;
    result.valid = std::isfinite(best_abs_error);
    return result;
}

PredictedBurnSolution FiniteBurnPredictor::solve(
    const SimulationEngine& engine,
    const StateEstimator& estimator,
    double target_semi_major_axis_meters,
    double maximum_delta_v_m_per_s) const {
    PredictedBurnSolution result{};
    if (target_semi_major_axis_meters <= 0.0 || maximum_delta_v_m_per_s <= 0.0) {
        return result;
    }

    const auto current = EarthCenteredNavigator{}.determine(estimator.state(), engine.primary_body());
    if (!std::isfinite(current.orbit.semi_major_axis_meters)) {
        return result;
    }
    const double current_error = current.orbit.semi_major_axis_meters - target_semi_major_axis_meters;
    if (std::abs(current_error) <= 1.0e-6) {
        result.valid = true;
        result.predicted_semi_major_axis_meters = current.orbit.semi_major_axis_meters;
        result.predicted_eccentricity = current.orbit.eccentricity;
        return result;
    }

    const int direction = direction_for_target(engine, estimator, target_semi_major_axis_meters);
    if (direction == 0) {
        return result;
    }

    const auto at_zero = current;
    const auto at_max = predict(engine, estimator, maximum_delta_v_m_per_s, direction);
    const double f0 = at_zero.orbit.semi_major_axis_meters - target_semi_major_axis_meters;
    const double fmax = at_max.orbit.semi_major_axis_meters - target_semi_major_axis_meters;

    double best_dv = 0.0;
    NavigationState best = at_zero;
    double best_abs_error = std::abs(f0);

    if (std::abs(fmax) < best_abs_error) {
        best_dv = maximum_delta_v_m_per_s;
        best = at_max;
        best_abs_error = std::abs(fmax);
    }

    if ((f0 > 0.0 && fmax < 0.0) || (f0 < 0.0 && fmax > 0.0)) {
        double low = 0.0;
        double high = maximum_delta_v_m_per_s;
        for (int i = 0; i < kBisectionIterations; ++i) {
            const double mid = 0.5 * (low + high);
            const auto candidate = predict(engine, estimator, mid, direction);
            const double fm = candidate.orbit.semi_major_axis_meters - target_semi_major_axis_meters;
            if (std::abs(fm) < best_abs_error) {
                best_abs_error = std::abs(fm);
                best_dv = mid;
                best = candidate;
            }
            if ((f0 <= 0.0 && fm <= 0.0) || (f0 >= 0.0 && fm >= 0.0)) {
                low = mid;
            } else {
                high = mid;
            }
        }
    }

    result.delta_v_m_per_s = static_cast<double>(direction) * best_dv;
    result.direction_sign = direction;
    result.duration_seconds = duration_for_delta_v(engine, best_dv);
    result.predicted_semi_major_axis_meters = best.orbit.semi_major_axis_meters;
    result.predicted_eccentricity = best.orbit.eccentricity;
    result.prediction_error_meters = best_abs_error;
    result.valid = std::isfinite(best_abs_error);
    return result;
}

} // namespace trishula
