#include "trishula/maneuver/physical_lunar_powered_descent.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace trishula {
namespace {
constexpr double kG0 = 9.80665;
constexpr double kEpsilon = 1.0e-9;
constexpr double kMinimumMassMargin = 1.0e-6;

Vector3 relative_position_of(
    const SimulationEngine& simulation,
    const PhysicalLunarPoweredDescentExecutor::EphemerisFunction& moon_position) {
    return simulation.spacecraft().state().position_meters -
        moon_position(simulation.clock().time());
}

Vector3 relative_velocity_of(
    const SimulationEngine& simulation,
    const PhysicalLunarPoweredDescentExecutor::EphemerisFunction& moon_velocity) {
    return simulation.spacecraft().state().velocity_m_per_s -
        moon_velocity(simulation.clock().time());
}

} // namespace

PhysicalLunarPoweredDescentExecutor::PhysicalLunarPoweredDescentExecutor(
    PhysicalLunarPoweredDescentConfiguration configuration,
    EphemerisFunction moon_position,
    EphemerisFunction moon_velocity,
    double moon_gravitational_parameter_m3_s2,
    double moon_radius_meters)
    : configuration_(configuration),
      moon_position_(std::move(moon_position)),
      moon_velocity_(std::move(moon_velocity)),
      moon_gravitational_parameter_m3_s2_(moon_gravitational_parameter_m3_s2),
      moon_radius_meters_(moon_radius_meters) {
    if (configuration_.initiation_altitude_meters <= configuration_.powered_descent_checkpoint_altitude_meters ||
        configuration_.powered_descent_checkpoint_altitude_meters <= 0.0) {
        throw std::invalid_argument("Powered descent altitude window is invalid");
    }
    if (configuration_.deorbit_target_perilune_altitude_meters >= 0.0) {
        throw std::invalid_argument("Powered descent deorbit target must intersect below the lunar surface");
    }
    if (configuration_.control_step_seconds <= 0.0 ||
        configuration_.maximum_descent_duration_seconds <= 0.0 ||
        configuration_.maximum_deorbit_delta_v_m_per_s <= 0.0) {
        throw std::invalid_argument("Powered descent configuration contains a non-positive limit");
    }
    if (!moon_position_ || !moon_velocity_ ||
        moon_gravitational_parameter_m3_s2_ <= 0.0 || moon_radius_meters_ <= 0.0) {
        throw std::invalid_argument("Powered descent requires a valid lunar ephemeris");
    }
}

Vector3 PhysicalLunarPoweredDescentExecutor::relative_position(
    const SimulationEngine& simulation) const {
    return relative_position_of(simulation, moon_position_);
}

Vector3 PhysicalLunarPoweredDescentExecutor::relative_velocity(
    const SimulationEngine& simulation) const {
    return relative_velocity_of(simulation, moon_velocity_);
}

double PhysicalLunarPoweredDescentExecutor::altitude(
    const SimulationEngine& simulation) const {
    return relative_position(simulation).magnitude() - moon_radius_meters_;
}

double PhysicalLunarPoweredDescentExecutor::radial_velocity(
    const SimulationEngine& simulation) const {
    const Vector3 r = relative_position(simulation);
    const Vector3 v = relative_velocity(simulation);
    return r.dot(v) / r.magnitude();
}

double PhysicalLunarPoweredDescentExecutor::tangential_speed(
    const SimulationEngine& simulation) const {
    const Vector3 r = relative_position(simulation);
    const Vector3 v = relative_velocity(simulation);
    const Vector3 radial = r.normalized();
    return (v - radial * v.dot(radial)).magnitude();
}

double PhysicalLunarPoweredDescentExecutor::delta_v_for_target_perilune(
    const SimulationEngine& simulation,
    double target_perilune_radius_meters) const {
    const Vector3 r = relative_position(simulation);
    const Vector3 v = relative_velocity(simulation);
    const double radius = r.magnitude();
    const Vector3 radial = r.normalized();
    const double radial_rate = radial.dot(v);
    const Vector3 tangential_vector = v - radial * radial_rate;
    const double tangential = tangential_vector.magnitude();
    if (radius <= target_perilune_radius_meters || tangential <= kEpsilon) {
        throw std::runtime_error("Cannot compute physical lunar deorbit burn from current state");
    }

    // V0.9.45 ends at the measured low perilune. Treat the current point as
    // the burn point and solve the osculating ellipse that uses it as apolune.
    const double target_a = 0.5 * (radius + target_perilune_radius_meters);
    const double target_speed = std::sqrt(
        moon_gravitational_parameter_m3_s2_ *
        (2.0 / radius - 1.0 / target_a));
    return std::max(0.0, tangential - target_speed);
}

double PhysicalLunarPoweredDescentExecutor::burn_duration_for_delta_v(
    const SimulationEngine& simulation,
    double delta_v_m_per_s) const {
    if (delta_v_m_per_s <= 0.0) return 0.0;
    const double mass = simulation.spacecraft().state().mass_kg;
    const double dry_mass = simulation.main_engine().dry_mass_kg();
    const double isp = simulation.main_engine().specific_impulse_seconds();
    const double thrust = simulation.main_engine().maximum_thrust_newtons();
    if (mass <= dry_mass + kMinimumMassMargin) {
        throw std::runtime_error("Powered descent has insufficient propellant");
    }
    const double final_mass = mass * std::exp(-delta_v_m_per_s / (isp * kG0));
    if (final_mass < dry_mass) {
        throw std::runtime_error("Powered descent deorbit burn exceeds available propellant");
    }
    const double mdot = thrust / (isp * kG0);
    return (mass - final_mass) / mdot;
}

double PhysicalLunarPoweredDescentExecutor::execute_retrograde_burn(
    SimulationEngine& simulation,
    double delta_v_m_per_s) const {
    if (delta_v_m_per_s <= 0.0) return 0.0;

    const double duration = burn_duration_for_delta_v(simulation, delta_v_m_per_s);
    const double initial_mass = simulation.spacecraft().state().mass_kg;
    double remaining = duration;
    double achieved_delta_v = 0.0;

    while (remaining > kEpsilon) {
        const double dt = std::min(configuration_.control_step_seconds, remaining);
        const TrueState state = simulation.spacecraft().state();
        const Vector3 r = state.position_meters - moon_position_(simulation.clock().time());
        const Vector3 moon_v = moon_velocity_(simulation.clock().time());
        const Vector3 v = state.velocity_m_per_s - moon_v;
        const Vector3 radial = r.normalized();
        const Vector3 tangential = v - radial * v.dot(radial);
        if (tangential.magnitude() <= kEpsilon) {
            throw std::runtime_error("Powered descent lost a valid retrograde direction");
        }
        const Vector3 inertial_direction = tangential.normalized() * -1.0;
        const Vector3 body_direction =
            state.attitude_body_to_inertial.inverse().rotate(inertial_direction);

        PropulsionCommand command{};
        command.main_engine_throttle = 1.0;
        command.commanded_thrust_direction_body = body_direction;
        simulation.step_for_duration(command, dt);

        const auto& output = simulation.last_propulsion_output();
        if (state.mass_kg > 0.0) {
            achieved_delta_v += output.thrust_force_inertial_newtons.magnitude() /
                state.mass_kg * dt;
        }
        remaining -= dt;
    }

    const double consumed = initial_mass - simulation.spacecraft().state().mass_kg;
    if (consumed <= 0.0) {
        throw std::runtime_error("Powered descent deorbit burn consumed no propellant");
    }
    return achieved_delta_v;
}

PhysicalLunarPoweredDescentResult PhysicalLunarPoweredDescentExecutor::execute(
    SimulationEngine& simulation) const {
    PhysicalLunarPoweredDescentResult result{};

    const double initial_altitude = altitude(simulation);
    result.initial_altitude_meters = initial_altitude;
    result.deorbit_target_perilune_altitude_meters =
        configuration_.deorbit_target_perilune_altitude_meters;

    const TrueState initial_state = simulation.spacecraft().state();
    if (initial_altitude <= configuration_.powered_descent_checkpoint_altitude_meters ||
        initial_altitude > configuration_.initiation_altitude_meters + 25000.0 ||
        initial_state.mass_kg <= simulation.main_engine().dry_mass_kg() + kMinimumMassMargin) {
        throw std::runtime_error("Current physical state is not a valid V0.9.46 descent-initiation state");
    }
    result.valid_initial_state = true;

    const double target_perilune_radius =
        moon_radius_meters_ + configuration_.deorbit_target_perilune_altitude_meters;
    if (target_perilune_radius <= 0.0) {
        throw std::runtime_error("Powered descent target perilune radius is non-physical");
    }

    const double planned_deorbit_dv =
        delta_v_for_target_perilune(simulation, target_perilune_radius);
    if (planned_deorbit_dv <= 0.0 || planned_deorbit_dv > configuration_.maximum_deorbit_delta_v_m_per_s) {
        throw std::runtime_error("Powered descent deorbit delta-v is outside configured limits");
    }
    result.planned_deorbit_delta_v_m_per_s = planned_deorbit_dv;
    result.deorbit_burn_duration_seconds = burn_duration_for_delta_v(simulation, planned_deorbit_dv);
    const double initial_mass = simulation.spacecraft().state().mass_kg;
    result.executed_deorbit_delta_v_m_per_s = execute_retrograde_burn(simulation, planned_deorbit_dv);
    result.deorbit_propellant_consumed_kg =
        initial_mass - simulation.spacecraft().state().mass_kg;
    result.deorbit_burn_executed = result.deorbit_propellant_consumed_kg > 0.0;

    // The deorbit burn turns the V0.9.45 perilune into an osculating apolune.
    // We then wait for an actual altitude/radial-velocity event rather than a
    // timer before declaring entry into powered descent.
    double elapsed = 0.0;
    while (elapsed < configuration_.maximum_descent_duration_seconds) {
        const double h = altitude(simulation);
        const double vr = radial_velocity(simulation);
        if (h <= configuration_.initiation_altitude_meters && vr < -0.1) {
            result.descent_entry_detected = true;
            result.descent_entry_altitude_meters = h;
            result.descent_entry_radial_velocity_m_per_s = vr;
            result.descent_entry_tangential_velocity_m_per_s = tangential_speed(simulation);
            break;
        }
        simulation.step_for_duration({}, configuration_.control_step_seconds);
        elapsed += configuration_.control_step_seconds;
        ++result.physical_propagation_steps;
    }

    if (!result.descent_entry_detected) {
        throw std::runtime_error("Physical powered-descent entry event was not detected");
    }

    const double entry_mass = simulation.spacecraft().state().mass_kg;
    double powered_elapsed = 0.0;
    const double entry_tangential_speed = result.descent_entry_tangential_velocity_m_per_s;
    while (powered_elapsed < configuration_.maximum_descent_duration_seconds) {
        const double h = altitude(simulation);
        if (h <= configuration_.powered_descent_checkpoint_altitude_meters) break;

        const Vector3 r = relative_position(simulation);
        const Vector3 v = relative_velocity(simulation);
        const double radius = r.magnitude();
        const Vector3 radial = r / radius;
        const double vr = radial.dot(v);
        const Vector3 transverse = v - radial * vr;
        const double vt = transverse.magnitude();
        const Vector3 tangential_hat = transverse.magnitude() > kEpsilon
            ? transverse.normalized() : Vector3{};

        const double g = moon_gravitational_parameter_m3_s2_ / (radius * radius);
        const double target_vr = -std::min(
            80.0,
            std::max(8.0, std::sqrt(std::max(1.0, 2.0 * g * h * 0.12))));
        const double altitude_fraction = std::clamp(
            (h - configuration_.powered_descent_checkpoint_altitude_meters) /
                (configuration_.initiation_altitude_meters - configuration_.powered_descent_checkpoint_altitude_meters),
            0.0, 1.0);
        const double target_vt = 200.0 +
            std::max(0.0, entry_tangential_speed - 200.0) * std::pow(altitude_fraction, 0.7);

        // Positive radial acceleration is outward. If the vehicle is already
        // descending faster than the target, the engine supplies no inward
        // command; lunar gravity remains responsible for the fall.
        double radial_acceleration =
            std::max(0.0, g + 0.8 * (target_vr - vr));
        double tangential_acceleration = 0.06 * (target_vt - vt);
        Vector3 desired_acceleration =
            radial * radial_acceleration + tangential_hat * tangential_acceleration;

        const double maximum_acceleration =
            simulation.main_engine().maximum_thrust_newtons() /
            simulation.spacecraft().state().mass_kg;
        const double requested_acceleration = desired_acceleration.magnitude();
        const double scale = requested_acceleration > maximum_acceleration
            ? maximum_acceleration / requested_acceleration : 1.0;
        desired_acceleration *= scale;

        const double throttle = maximum_acceleration > 0.0
            ? std::clamp(desired_acceleration.magnitude() / maximum_acceleration, 0.0, 1.0)
            : 0.0;

        PropulsionCommand command{};
        command.main_engine_throttle = throttle;
        if (desired_acceleration.magnitude() > kEpsilon) {
            command.commanded_thrust_direction_body =
                simulation.spacecraft().state().attitude_body_to_inertial.inverse().rotate(
                    desired_acceleration.normalized());
        }
        simulation.step_for_duration(command, configuration_.control_step_seconds);
        powered_elapsed += configuration_.control_step_seconds;
        ++result.physical_propagation_steps;
    }

    result.powered_descent_duration_seconds = powered_elapsed;
    result.powered_descent_propellant_consumed_kg =
        entry_mass - simulation.spacecraft().state().mass_kg;
    result.final_altitude_meters = altitude(simulation);
    result.final_radial_velocity_m_per_s = radial_velocity(simulation);
    result.final_tangential_velocity_m_per_s = tangential_speed(simulation);
    result.powered_descent_active =
        result.descent_entry_detected &&
        result.powered_descent_duration_seconds > 0.0 &&
        result.powered_descent_propellant_consumed_kg > 0.0 &&
        result.final_altitude_meters <= configuration_.initiation_altitude_meters &&
        result.final_altitude_meters > configuration_.powered_descent_checkpoint_altitude_meters - 250.0;

    return result;
}

} // namespace trishula
