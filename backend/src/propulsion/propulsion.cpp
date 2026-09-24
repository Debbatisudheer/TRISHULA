#include "trishula/propulsion/propulsion.h"

#include <algorithm>
#include <stdexcept>

namespace trishula {

namespace {
constexpr double kStandardGravityMPerS2 = 9.80665;
constexpr double kTolerance = 1e-12;

PropulsionOutput empty_output(const TrueState& state, double dry_mass_kg) {
    return {
        {}, {}, {}, {}, {}, 0.0, 0.0,
        std::max(0.0, state.mass_kg - dry_mass_kg), false, false
    };
}
}

MainEngine::MainEngine(double maximum_thrust_newtons,
                       double specific_impulse_seconds,
                       double dry_mass_kg)
    : maximum_thrust_newtons_(maximum_thrust_newtons),
      specific_impulse_seconds_(specific_impulse_seconds),
      dry_mass_kg_(dry_mass_kg) {
    if (maximum_thrust_newtons <= 0.0) throw std::invalid_argument("Maximum engine thrust must be positive");
    if (specific_impulse_seconds <= 0.0) throw std::invalid_argument("Specific impulse must be positive");
    if (dry_mass_kg < 0.0) throw std::invalid_argument("Dry mass cannot be negative");
}

double MainEngine::maximum_thrust_newtons() const noexcept { return maximum_thrust_newtons_; }
double MainEngine::specific_impulse_seconds() const noexcept { return specific_impulse_seconds_; }
double MainEngine::dry_mass_kg() const noexcept { return dry_mass_kg_; }

PropulsionOutput MainEngine::evaluate(const PropulsionCommand& command,
                                      const TrueState& state,
                                      double time_step_seconds) const {
    if (time_step_seconds <= 0.0) throw std::invalid_argument("Propulsion time step must be positive");
    if (state.mass_kg < dry_mass_kg_ - kTolerance) throw std::invalid_argument("Spacecraft mass cannot be below dry mass");
    if (state.mass_kg <= 0.0) throw std::invalid_argument("Spacecraft mass must be positive");

    const double throttle = std::clamp(command.main_engine_throttle, 0.0, 1.0);
    const double commanded_thrust = maximum_thrust_newtons_ * throttle;
    const bool engine_firing = commanded_thrust > 0.0 && state.mass_kg > dry_mass_kg_ + kTolerance;
    if (!engine_firing) return empty_output(state, dry_mass_kg_);

    const double mass_flow_rate = commanded_thrust / (specific_impulse_seconds_ * kStandardGravityMPerS2);
    const double available_fuel = std::max(0.0, state.mass_kg - dry_mass_kg_);
    const double consumed_fuel = std::min(available_fuel, mass_flow_rate * time_step_seconds);
    const double actual_mass_flow = consumed_fuel / time_step_seconds;
    const double actual_thrust = actual_mass_flow * specific_impulse_seconds_ * kStandardGravityMPerS2;

    Vector3 thrust_direction = command.commanded_thrust_direction_body;
    thrust_direction = thrust_direction.magnitude() <= kTolerance ? Vector3{1.0, 0.0, 0.0} : thrust_direction.normalized();
    const Vector3 thrust_force_body = thrust_direction * actual_thrust;
    const Vector3 thrust_force_inertial = state.attitude_body_to_inertial.rotate(thrust_force_body);

    return {
        thrust_force_body, thrust_force_inertial, {}, {},
        command.commanded_body_torque_nm, actual_mass_flow, consumed_fuel,
        available_fuel - consumed_fuel, true, false
    };
}

RcsModule::RcsModule(double maximum_force_newtons,
                     double specific_impulse_seconds,
                     double dry_mass_kg)
    : maximum_force_newtons_(maximum_force_newtons),
      specific_impulse_seconds_(specific_impulse_seconds),
      dry_mass_kg_(dry_mass_kg) {
    if (maximum_force_newtons <= 0.0) throw std::invalid_argument("Maximum RCS force must be positive");
    if (specific_impulse_seconds <= 0.0) throw std::invalid_argument("RCS specific impulse must be positive");
    if (dry_mass_kg < 0.0) throw std::invalid_argument("Dry mass cannot be negative");
}

double RcsModule::maximum_force_newtons() const noexcept { return maximum_force_newtons_; }
double RcsModule::specific_impulse_seconds() const noexcept { return specific_impulse_seconds_; }

PropulsionOutput RcsModule::evaluate(const PropulsionCommand& command,
                                     const TrueState& state,
                                     double time_step_seconds) const {
    if (time_step_seconds <= 0.0) throw std::invalid_argument("RCS time step must be positive");
    if (state.mass_kg <= 0.0) throw std::invalid_argument("Spacecraft mass must be positive");
    if (state.mass_kg < dry_mass_kg_ - kTolerance) throw std::invalid_argument("Spacecraft mass cannot be below dry mass");

    const double requested = command.commanded_rcs_force_body_newtons.magnitude();
    const double usable_fuel = std::max(0.0, state.mass_kg - dry_mass_kg_);
    if (requested <= kTolerance || usable_fuel <= kTolerance) return empty_output(state, dry_mass_kg_);

    const double actual_force_magnitude = std::min(requested, maximum_force_newtons_);
    const Vector3 direction = command.commanded_rcs_force_body_newtons.normalized();
    const double mass_flow_rate = actual_force_magnitude / (specific_impulse_seconds_ * kStandardGravityMPerS2);
    const double consumed_fuel = std::min(usable_fuel, mass_flow_rate * time_step_seconds);
    const double actual_mass_flow = consumed_fuel / time_step_seconds;
    const double actual_force = actual_mass_flow * specific_impulse_seconds_ * kStandardGravityMPerS2;
    const Vector3 force_body = direction * actual_force;
    const Vector3 force_inertial = state.attitude_body_to_inertial.rotate(force_body);

    return {
        {}, {}, force_body, force_inertial, command.commanded_body_torque_nm,
        actual_mass_flow, consumed_fuel, usable_fuel - consumed_fuel, false, true
    };
}

} // namespace trishula
