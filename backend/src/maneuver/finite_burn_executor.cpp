#include "trishula/maneuver/finite_burn_executor.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kEpsilon = 1e-12;

Vector3 inertial_prograde_direction(const TrueState& state, const CelestialBody& body) {
    const Vector3 relative_position = state.position_meters - body.center_position();
    const double radius = relative_position.magnitude();
    if (radius <= kEpsilon) {
        throw std::runtime_error("Cannot determine prograde direction at zero radius");
    }

    const Vector3 radial = relative_position / radius;
    const double radial_velocity = radial.dot(state.velocity_m_per_s);
    const Vector3 tangential = state.velocity_m_per_s - radial * radial_velocity;
    if (tangential.magnitude() <= kEpsilon) {
        throw std::runtime_error("Cannot determine prograde direction with zero tangential velocity");
    }
    return tangential.normalized();
}

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

} // namespace

HohmannFiniteBurnExecutor::HohmannFiniteBurnExecutor(double step_seconds)
    : step_seconds_(step_seconds) {
    if (step_seconds <= 0.0) {
        throw std::invalid_argument("Execution step must be positive");
    }
}

NavigationState HohmannFiniteBurnExecutor::navigate_truth(
    const SimulationEngine& engine) const {
    EarthCenteredNavigator navigator;
    return navigator.determine(
        truth_as_estimated(engine.spacecraft().state()),
        engine.primary_body());
}

BurnExecutionSummary HohmannFiniteBurnExecutor::execute_burn(
    SimulationEngine& engine,
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
    double integrated_thrust_acceleration = 0.0;

    while (remaining > kEpsilon) {
        const double dt = std::min(step_seconds_, remaining);
        const TrueState state = engine.spacecraft().state();
        const Vector3 prograde = inertial_prograde_direction(state, engine.primary_body());
        const Vector3 inertial_direction = prograde * static_cast<double>(direction_sign);
        const Vector3 body_direction = state.attitude_body_to_inertial.inverse().rotate(inertial_direction);

        PropulsionCommand command{};
        command.main_engine_throttle = 1.0;
        command.commanded_thrust_direction_body = body_direction;

        engine.step_for_duration(command, dt);
        ++simulation_steps;

        const PropulsionOutput& output = engine.last_propulsion_output();
        if (state.mass_kg > 0.0) {
            integrated_thrust_acceleration +=
                output.thrust_force_inertial_newtons.magnitude() / state.mass_kg * dt;
        }
        remaining -= dt;
    }

    summary.end_time_seconds = engine.spacecraft().state().time_seconds;
    summary.duration_seconds = summary.end_time_seconds - summary.start_time_seconds;
    summary.final_mass_kg = engine.spacecraft().state().mass_kg;
    summary.propellant_consumed_kg = summary.initial_mass_kg - summary.final_mass_kg;
    summary.achieved_propulsive_delta_v_m_per_s = integrated_thrust_acceleration;
    return summary;
}

ManeuverExecutionResult HohmannFiniteBurnExecutor::execute(
    SimulationEngine& engine,
    const OrbitalManeuverPlan& plan) const {
    if (plan.first_burn_direction_sign == 0 || plan.second_burn_direction_sign == 0) {
        throw std::invalid_argument("Finite-burn execution requires a non-zero maneuver direction");
    }

    ManeuverExecutionResult result{};
    result.plan = plan;
    result.initial_orbit = navigate_truth(engine);

    result.first_burn = execute_burn(
        engine,
        plan.first_burn_duration_seconds,
        plan.first_burn_direction_sign,
        result.simulation_steps);
    result.after_first_burn = navigate_truth(engine);

    double previous_radial_velocity = result.after_first_burn.radial_velocity_m_per_s;
    double coast_time = 0.0;
    constexpr double kMinimumCoastBeforeEventSeconds = 5.0;
    const double max_coast_seconds = plan.transfer_time_seconds * 1.25 + 60.0;

    while (coast_time < max_coast_seconds) {
        engine.step({});
        ++result.simulation_steps;
        coast_time += step_seconds_;

        const NavigationState navigation = navigate_truth(engine);
        const double current_radial_velocity = navigation.radial_velocity_m_per_s;

        if (coast_time >= kMinimumCoastBeforeEventSeconds &&
            previous_radial_velocity > 0.0 && current_radial_velocity <= 0.0) {
            result.before_second_burn = navigation;
            result.apoapsis_event_detected = true;
            break;
        }

        previous_radial_velocity = current_radial_velocity;
    }

    if (!result.apoapsis_event_detected) {
        result.before_second_burn = navigate_truth(engine);
        throw std::runtime_error("Failed to detect transfer apoapsis before timeout");
    }

    result.coast_duration_seconds = coast_time;
    result.second_burn = execute_burn(
        engine,
        plan.second_burn_duration_seconds,
        plan.second_burn_direction_sign,
        result.simulation_steps);
    result.final_orbit = navigate_truth(engine);
    return result;
}

} // namespace trishula
