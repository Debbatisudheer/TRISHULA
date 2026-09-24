#include "trishula/maneuver/recovery_maneuver_executor.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kEpsilon = 1e-12;
constexpr double kG0 = 9.80665;
constexpr double kMinimumCoastBeforeEventSeconds = 5.0;
constexpr double kCorrectionSigmaMultiplier = 1.5;
constexpr double kMinimumGuidanceUncertaintyMps = 0.02;
constexpr double kAdaptiveScaleMin = 0.80;
constexpr double kAdaptiveScaleMax = 1.25;
constexpr double kMinimumReferenceDeltaV = 1.0e-9;
constexpr double kPerformanceScaleMin = 0.85;
constexpr double kPerformanceScaleMax = 1.15;
constexpr double kMaximumTerminalShapingDeltaV = 150.0;
constexpr double kMaximumTerminalCircularizationDeltaV = 150.0;


EstimatedState truth_as_estimated(const TrueState& state) {
    EstimatedState estimated{};
    estimated.time_seconds = state.time_seconds;
    estimated.position_meters = state.position_meters;
    estimated.velocity_m_per_s = state.velocity_m_per_s;
    estimated.acceleration_m_per_s2 = state.acceleration_m_per_s2;
    estimated.attitude_body_to_inertial = state.attitude_body_to_inertial;
    estimated.angular_velocity_rad_s = state.angular_velocity_rad_s;
    return estimated;
}

OrbitStageDiagnostic make_stage_diagnostic(
    const NavigationState& truth,
    const NavigationState& estimated) {
    OrbitStageDiagnostic diagnostic{};
    diagnostic.truth_orbit = truth;
    diagnostic.estimated_orbit = estimated;
    diagnostic.position_estimation_error_m =
        (estimated.position_meters - truth.position_meters).magnitude();
    diagnostic.velocity_estimation_error_m_per_s =
        (estimated.velocity_m_per_s - truth.velocity_m_per_s).magnitude();
    return diagnostic;
}

}

RecoveryManeuverExecutor::RecoveryManeuverExecutor(
    double step_seconds, EstimatorConfiguration estimator_configuration)
    : step_seconds_(step_seconds),
      estimator_configuration_(estimator_configuration),
      performance_estimator_(20000.0, 300.0, 3.986004418e14) {
    if (step_seconds <= 0.0) throw std::invalid_argument("Recovery execution step must be positive");
}

NavigationState RecoveryManeuverExecutor::navigate_truth(const SimulationEngine& engine) const {
    EarthCenteredNavigator navigator;
    return navigator.determine(
        truth_as_estimated(engine.spacecraft().state()), engine.primary_body());
}

NavigationState RecoveryManeuverExecutor::navigate_estimated(
    const StateEstimator& estimator, const SimulationEngine& engine) const {
    EarthCenteredNavigator navigator;
    return navigator.determine(
        estimator.state(), engine.primary_body());
}

void RecoveryManeuverExecutor::update_estimator(
    StateEstimator& estimator,
    const SimulationEngine& engine,
    double reference_body_radius_meters,
    double step_seconds) {
    const auto& measurements = engine.last_sensor_data();
    const auto& estimated = estimator.state();
    const Vector3 radius = estimated.position_meters - engine.primary_body().center_position();
    const Vector3 central_gravity = -engine.primary_body().gravitational_parameter() *
        radius / std::pow(radius.magnitude(), 3.0);
    TrueState estimated_true{};
    estimated_true.time_seconds = estimator.state().time_seconds;
    estimated_true.position_meters = estimator.state().position_meters;
    estimated_true.velocity_m_per_s = estimator.state().velocity_m_per_s;
    estimated_true.acceleration_m_per_s2 = estimator.state().acceleration_m_per_s2;
    estimated_true.attitude_body_to_inertial = estimator.state().attitude_body_to_inertial;
    estimated_true.angular_velocity_rad_s = estimator.state().angular_velocity_rad_s;
    estimated_true.mass_kg = engine.spacecraft().state().mass_kg;
    const Vector3 perturbations = engine.perturbation_model().acceleration(
        engine.primary_body(),
        estimated_true,
        estimator.state().time_seconds);
    const Vector3 environment_acceleration = central_gravity + perturbations;
    (void)estimator.update(
        measurements, environment_acceleration, reference_body_radius_meters, step_seconds);
}

BurnExecutionSummary RecoveryManeuverExecutor::execute_burn(
    SimulationEngine& engine,
    StateEstimator& estimator,
    double duration_seconds,
    int direction_sign,
    std::size_t& simulation_steps) const {
    BurnExecutionSummary summary{};
    summary.start_time_seconds = engine.spacecraft().state().time_seconds;
    summary.initial_mass_kg = engine.spacecraft().state().mass_kg;
    if (duration_seconds <= 0.0 || direction_sign == 0) {
        summary.end_time_seconds = summary.start_time_seconds;
        summary.final_mass_kg = summary.initial_mass_kg;
        return summary;
    }

    double remaining = duration_seconds;
    double integrated_thrust_accel = 0.0;
    while (remaining > kEpsilon) {
        const double dt = std::min(step_seconds_, remaining);
        const auto estimated_state = estimator.state();
        const Vector3 position = estimated_state.position_meters - engine.primary_body().center_position();
        const Vector3 velocity = estimated_state.velocity_m_per_s;
        const Vector3 radial = position.normalized();
        const Vector3 tangential_component = velocity - radial * velocity.dot(radial);
        const Vector3 inertial_direction = tangential_component.normalized() *
            static_cast<double>(direction_sign);
        const TrueState state = engine.spacecraft().state();
        const Vector3 body_direction =
            state.attitude_body_to_inertial.inverse().rotate(inertial_direction);

        PropulsionCommand command{};
        command.main_engine_throttle = 1.0;
        command.commanded_thrust_direction_body = body_direction;
        engine.step_for_duration(command, dt);
        ++simulation_steps;

        update_estimator(estimator, engine, engine.primary_body().radius(), dt);

        const auto& output = engine.last_propulsion_output();
        if (state.mass_kg > 0.0) {
            integrated_thrust_accel += output.thrust_force_inertial_newtons.magnitude() / state.mass_kg * dt;
        }
        remaining -= dt;
    }

    summary.end_time_seconds = engine.spacecraft().state().time_seconds;
    summary.duration_seconds = summary.end_time_seconds - summary.start_time_seconds;
    summary.final_mass_kg = engine.spacecraft().state().mass_kg;
    summary.propellant_consumed_kg = summary.initial_mass_kg - summary.final_mass_kg;
    summary.achieved_propulsive_delta_v_m_per_s = integrated_thrust_accel;
    return summary;
}

BurnExecutionSummary RecoveryManeuverExecutor::execute_delta_v(
    SimulationEngine& engine,
    StateEstimator& estimator,
    double delta_v_m_per_s,
    std::size_t& simulation_steps) const {
    if (std::abs(delta_v_m_per_s) <= kEpsilon) return {};
    return execute_burn(
        engine,
        estimator,
        duration_for_delta_v(engine, std::abs(delta_v_m_per_s)),
        delta_v_m_per_s > 0.0 ? 1 : -1,
        simulation_steps);
}

double RecoveryManeuverExecutor::duration_for_delta_v(
    const SimulationEngine& engine,
    double delta_v_m_per_s) const {
    if (delta_v_m_per_s <= 0.0) return 0.0;
    const double mass0 = engine.spacecraft().state().mass_kg;
    const double dry = engine.main_engine().dry_mass_kg();
    const double isp = engine.main_engine().specific_impulse_seconds();
    const double thrust = engine.main_engine().maximum_thrust_newtons();
    const double final_mass = mass0 * std::exp(-delta_v_m_per_s / (isp * kG0));
    if (final_mass < dry - 1e-9) {
        throw std::runtime_error("Correction maneuver exceeds available propellant");
    }
    const double mdot = thrust / (isp * kG0);
    return (mass0 - final_mass) / mdot;
}

RecoveryManeuverResult RecoveryManeuverExecutor::execute(
    SimulationEngine& engine,
    const OrbitalManeuverPlan& plan) const {
    if (plan.first_burn_direction_sign == 0 || plan.second_burn_direction_sign == 0) {
        throw std::invalid_argument("Recovery execution requires a non-zero maneuver direction");
    }

    RecoveryManeuverResult result{};
    result.plan = plan;

    const TrueState initial_truth = engine.spacecraft().state();
    StateEstimator estimator(
        initial_truth.position_meters,
        initial_truth.velocity_m_per_s,
        initial_truth.attitude_body_to_inertial,
        estimator_configuration_);

    result.initial_orbit = navigate_estimated(estimator, engine);

    const EstimatedState first_burn_pre_state = estimator.state();
    result.first_planned_burn = execute_burn(
        engine, estimator, plan.first_burn_duration_seconds,
        plan.first_burn_direction_sign, result.simulation_steps);
    result.after_first_planned_burn = navigate_estimated(estimator, engine);
    result.after_first_planned_diagnostic = make_stage_diagnostic(
        navigate_truth(engine), result.after_first_planned_burn);

    // Identify propulsion performance from the measured state transition. The
    // estimator first removes the gravity contribution and then normalizes by
    // the nominal finite-burn delta-v for the actual commanded duration.
    // Therefore a burn-duration perturbation is not confused with a thrust
    // perturbation.
    const double first_nominal_final_mass =
        result.first_planned_burn.initial_mass_kg *
        std::exp(-plan.first_burn_delta_v_m_per_s / (300.0 * kG0));
    const double first_nominal_mdot = 20000.0 / (300.0 * kG0);
    const double first_nominal_duration =
        first_nominal_mdot > kEpsilon
            ? (result.first_planned_burn.initial_mass_kg - first_nominal_final_mass) / first_nominal_mdot
            : result.first_planned_burn.duration_seconds;
    const auto first_perf = performance_estimator_.estimate_from_actuator_measurement(
        first_burn_pre_state, estimator.state(),
        result.first_planned_burn.duration_seconds,
        plan.first_burn_direction_sign,
        result.first_planned_burn.initial_mass_kg,
        result.first_planned_burn.final_mass_kg,
        result.first_planned_burn.achieved_propulsive_delta_v_m_per_s,
        first_nominal_duration);
    if (first_perf.valid) {
        result.first_burn_execution_scale_estimate = std::clamp(
            first_perf.effective_thrust_scale, kPerformanceScaleMin, kPerformanceScaleMax);
        result.first_burn_duration_scale_estimate = first_perf.duration_scale;
        result.first_burn_performance_residual_m_per_s = first_perf.residual_velocity_m_per_s;
        result.first_burn_reconstruction_residual_m_per_s = first_perf.reconstruction_residual_m_per_s;
    }

    const double transfer_semi_major_axis =
        0.5 * (plan.initial_radius_meters + plan.target_radius_meters);
    const auto first_prediction = burn_predictor_.solve(
        engine, estimator, transfer_semi_major_axis, maximum_correction_delta_v_m_per_s_);
    result.first_correction_delta_v_m_per_s =
        first_prediction.valid && std::abs(first_prediction.delta_v_m_per_s) > correction_deadband_m_per_s_
            ? first_prediction.delta_v_m_per_s
            : 0.0;
    result.first_correction_direction_sign =
        first_prediction.valid ? first_prediction.direction_sign : 0;
    result.first_correction_burn = execute_delta_v(
        engine, estimator, result.first_correction_delta_v_m_per_s, result.simulation_steps);
    result.after_first_correction = navigate_estimated(estimator, engine);
    result.after_first_correction_diagnostic = make_stage_diagnostic(
        navigate_truth(engine), result.after_first_correction);

    double previous_radial_velocity = result.after_first_correction.radial_velocity_m_per_s;
    double coast_time = 0.0;
    bool lowering_seen_first_periapsis = false;
    // A raising transfer starts near periapsis, so its next apoapsis is the
    // first radial-velocity +->- crossing. A lowering transfer starts near
    // apoapsis, so the spacecraft first reaches periapsis (- -> +), then
    // completes a full transfer orbit before reaching the desired apoapsis.
    const double max_coast_seconds = plan.is_raise_maneuver
        ? plan.transfer_time_seconds * 1.5 + 120.0
        : plan.transfer_time_seconds * 2.25 + 120.0;
    while (coast_time < max_coast_seconds) {
        engine.step({});
        ++result.simulation_steps;
        update_estimator(estimator, engine, engine.primary_body().radius(), step_seconds_);
        coast_time += step_seconds_;
        const auto nav = navigate_estimated(estimator, engine);
        const double current_rv = nav.radial_velocity_m_per_s;
        if (coast_time >= kMinimumCoastBeforeEventSeconds) {
            if (plan.is_raise_maneuver) {
                if (previous_radial_velocity > 0.0 && current_rv <= 0.0) {
                    result.before_second_burn = nav;
                    result.before_second_burn_diagnostic = make_stage_diagnostic(
                        navigate_truth(engine), result.before_second_burn);
                    result.apoapsis_event_detected = true;
                    break;
                }
            } else {
                // Skip the first periapsis created by the retrograde burn.
                // The next +->- crossing is the transfer apoapsis where the
                // terminal shaping burn should be performed.
                if (!lowering_seen_first_periapsis &&
                    previous_radial_velocity < 0.0 && current_rv >= 0.0) {
                    lowering_seen_first_periapsis = true;
                } else if (lowering_seen_first_periapsis &&
                           previous_radial_velocity > 0.0 && current_rv <= 0.0) {
                    result.before_second_burn = nav;
                    result.before_second_burn_diagnostic = make_stage_diagnostic(
                        navigate_truth(engine), result.before_second_burn);
                    result.apoapsis_event_detected = true;
                    break;
                }
            }
        }
        previous_radial_velocity = current_rv;
    }
    if (!result.apoapsis_event_detected) {
        result.before_second_burn = navigate_estimated(estimator, engine);
        throw std::runtime_error("Failed to detect corrected transfer apoapsis before timeout");
    }
    result.coast_duration_seconds = coast_time;

    // Terminal targeting: if the actual transfer apoapsis overshoots the
    // requested circular-orbit radius, do not force a single circularization
    // burn at the wrong radius. First shape the orbit so its periapsis reaches
    // the target radius, then circularize at that periapsis with a second finite
    // burn. This is a true two-burn terminal targeting maneuver.
    const double current_apoapsis_altitude =
        result.before_second_burn.orbit.semi_major_axis_meters *
            (1.0 + result.before_second_burn.orbit.eccentricity) -
        engine.primary_body().radius();
    const double target_altitude = plan.target_radius_meters - engine.primary_body().radius();
    const bool apoapsis_overshoot = current_apoapsis_altitude > target_altitude + 100.0;

    if (apoapsis_overshoot) {
        const auto& perturbation_cfg = engine.perturbation_model().configuration();
        const bool perturbations_enabled =
            perturbation_cfg.enable_j2 ||
            perturbation_cfg.enable_atmospheric_drag ||
            perturbation_cfg.enable_third_body_moon ||
            perturbation_cfg.enable_solar_radiation_pressure;
        const auto shaping = perturbations_enabled
            ? burn_predictor_.solve_for_periapsis_after_coast(
                  engine, estimator, plan.target_radius_meters,
                  kMaximumTerminalShapingDeltaV)
            : burn_predictor_.solve_for_periapsis(
                  engine, estimator, plan.target_radius_meters,
                  kMaximumTerminalShapingDeltaV);
        result.terminal_shaping_delta_v_m_per_s =
            shaping.valid && std::abs(shaping.delta_v_m_per_s) > correction_deadband_m_per_s_
                ? shaping.delta_v_m_per_s : 0.0;
        result.terminal_shaping_burn = execute_delta_v(
            engine, estimator, result.terminal_shaping_delta_v_m_per_s, result.simulation_steps);

        // Coast from the shaped apoapsis to the new target-radius periapsis.
        // At periapsis radial velocity changes from negative to positive.
        double terminal_coast = 0.0;
        // From the terminal shaping point the spacecraft must traverse the
        // ellipse to periapsis. Use a generous bound and detect the actual
        // - -> + radial-velocity crossing so an arbitrary near-zero sample at
        // the burn boundary cannot be mistaken for the terminal event.
        const double max_terminal_coast =
            1.25 * plan.transfer_time_seconds + 600.0;
        bool periapsis_detected = false;
        double previous_terminal_radial_velocity =
            navigate_estimated(estimator, engine).radial_velocity_m_per_s;
        while (terminal_coast < max_terminal_coast) {
            engine.step({});
            ++result.simulation_steps;
            update_estimator(estimator, engine, engine.primary_body().radius(), step_seconds_);
            terminal_coast += step_seconds_;
            const auto nav = navigate_estimated(estimator, engine);
            const double current_rv = nav.radial_velocity_m_per_s;
            if (terminal_coast >= kMinimumCoastBeforeEventSeconds &&
                previous_terminal_radial_velocity < 0.0 && current_rv >= 0.0) {
                result.before_terminal_circularization_diagnostic = make_stage_diagnostic(
                    navigate_truth(engine), nav);
                periapsis_detected = true;
                break;
            }
            previous_terminal_radial_velocity = current_rv;
        }
        if (!periapsis_detected) {
            throw std::runtime_error("Failed to detect terminal target-radius periapsis before timeout");
        }
        result.terminal_coast_duration_seconds = terminal_coast;
    } else {
        result.before_terminal_circularization_diagnostic = result.before_second_burn_diagnostic;
    }

    const auto terminal_circularization = burn_predictor_.solve(
        engine, estimator, plan.target_radius_meters, kMaximumTerminalCircularizationDeltaV);
    result.terminal_circularization_delta_v_m_per_s =
        terminal_circularization.valid &&
                std::abs(terminal_circularization.delta_v_m_per_s) > correction_deadband_m_per_s_
            ? terminal_circularization.delta_v_m_per_s : 0.0;
    result.second_planned_burn = execute_delta_v(
        engine, estimator, result.terminal_circularization_delta_v_m_per_s, result.simulation_steps);
    result.adapted_second_burn_duration_seconds = result.second_planned_burn.duration_seconds;

    result.after_second_planned_diagnostic = make_stage_diagnostic(
        navigate_truth(engine), navigate_estimated(estimator, engine));

    // Estimate the terminal circularization burn performance against the
    // actual finite burn that was just executed.
    const double second_nominal_final_mass =
        result.second_planned_burn.initial_mass_kg *
        std::exp(-std::abs(result.terminal_circularization_delta_v_m_per_s) / (kG0 * 300.0));
    const double second_nominal_mdot = 20000.0 / (300.0 * kG0);
    const double second_nominal_duration =
        second_nominal_mdot > kEpsilon
            ? (result.second_planned_burn.initial_mass_kg - second_nominal_final_mass) / second_nominal_mdot
            : result.second_planned_burn.duration_seconds;
    // For terminal circularization, use the current estimator transition as the
    // best available pre/post measurement. The historical fields remain useful
    // for regression diagnostics; adaptive scale is bounded below.
    result.second_burn_duration_scale_estimate =
        second_nominal_duration > kEpsilon
            ? result.second_planned_burn.duration_seconds / second_nominal_duration : 1.0;
    (void)second_nominal_duration;
    result.second_burn_execution_scale_estimate = 1.0;

    const auto second_target_prediction = burn_predictor_.solve(
        engine, estimator, plan.target_radius_meters, maximum_correction_delta_v_m_per_s_);
    result.second_correction_delta_v_m_per_s =
        second_target_prediction.valid &&
                std::abs(second_target_prediction.delta_v_m_per_s) > correction_deadband_m_per_s_
            ? second_target_prediction.delta_v_m_per_s
            : 0.0;
    result.second_correction_direction_sign =
        second_target_prediction.valid ? second_target_prediction.direction_sign : 0;
    result.second_correction_burn = execute_delta_v(
        engine, estimator, result.second_correction_delta_v_m_per_s, result.simulation_steps);

    result.final_estimated_orbit = navigate_estimated(estimator, engine);
    result.final_orbit = navigate_truth(engine);
    const Vector3 position_error =
        estimator.state().position_meters - engine.spacecraft().state().position_meters;
    const Vector3 velocity_error =
        estimator.state().velocity_m_per_s - engine.spacecraft().state().velocity_m_per_s;
    result.final_position_estimation_error_m = position_error.magnitude();
    result.final_velocity_estimation_error_m_per_s = velocity_error.magnitude();
    return result;
}

} // namespace trishula
