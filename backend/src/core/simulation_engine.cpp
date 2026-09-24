#include "trishula/core/simulation_engine.h"

#include <utility>
#include <stdexcept>

namespace trishula {

SimulationEngine::SimulationEngine(
    CelestialBody primary_body,
    Spacecraft spacecraft,
    double time_step_seconds,
    MainEngine main_engine,
    RcsModule rcs_module,
    SensorSuite sensor_suite,
    PerturbationConfiguration perturbation_configuration)
    : clock_(time_step_seconds),
      primary_body_(primary_body),
      spacecraft_(spacecraft),
      main_engine_(main_engine),
      rcs_module_(rcs_module),
      sensor_suite_(std::move(sensor_suite)),
      perturbation_model_(perturbation_configuration) {}

void SimulationEngine::step(const PropulsionCommand& propulsion_command) {
    step_for_duration(propulsion_command, clock_.time_step());
}

void SimulationEngine::step_for_duration(const PropulsionCommand& propulsion_command, double duration_seconds) {
    if (duration_seconds <= 0.0) {
        throw std::invalid_argument("Simulation step duration must be positive");
    }
    const TrueState& current = spacecraft_.state();
    const double dt = duration_seconds;

    const Vector3 central_gravity_acceleration =
        gravity_model_.acceleration(primary_body_, current.position_meters);
    const Vector3 perturbation_acceleration =
        perturbation_model_.acceleration(primary_body_, current, clock_.time());
    const Vector3 environment_acceleration = central_gravity_acceleration + perturbation_acceleration;
    const PropulsionOutput main_output = main_engine_.evaluate(
        propulsion_command, current, dt);
    const PropulsionOutput rcs_output = rcs_module_.evaluate(
        propulsion_command, current, dt);

    last_propulsion_output_ = main_output;
    last_propulsion_output_.thrust_force_body_newtons =
        main_output.thrust_force_body_newtons;
    last_propulsion_output_.thrust_force_inertial_newtons =
        main_output.thrust_force_inertial_newtons;
    last_propulsion_output_.rcs_force_body_newtons =
        rcs_output.rcs_force_body_newtons;
    last_propulsion_output_.rcs_force_inertial_newtons =
        rcs_output.rcs_force_inertial_newtons;
    last_propulsion_output_.torque_body_nm = propulsion_command.commanded_body_torque_nm;
    last_propulsion_output_.fuel_mass_flow_kg_per_s =
        main_output.fuel_mass_flow_kg_per_s + rcs_output.fuel_mass_flow_kg_per_s;
    last_propulsion_output_.fuel_consumed_kg =
        std::min(current.mass_kg, main_output.fuel_consumed_kg + rcs_output.fuel_consumed_kg);
    last_propulsion_output_.remaining_fuel_mass_kg =
        std::max(0.0, current.mass_kg - last_propulsion_output_.fuel_consumed_kg - main_engine_.dry_mass_kg());
    last_propulsion_output_.engine_firing = main_output.engine_firing;
    last_propulsion_output_.rcs_firing = rcs_output.rcs_firing;

    const Vector3 gravity_force = environment_acceleration * current.mass_kg;
    const Vector3 total_force = gravity_force +
        main_output.thrust_force_inertial_newtons +
        rcs_output.rcs_force_inertial_newtons;
    const Vector3 acceleration =
        dynamics_.acceleration_from_force(total_force, current.mass_kg);

    const double next_mass =
        std::max(1.0e-12, current.mass_kg -
            std::min(current.mass_kg,
                     main_output.fuel_consumed_kg + rcs_output.fuel_consumed_kg));
    const Vector3 provisional_position =
        current.position_meters +
        current.velocity_m_per_s * dt +
        acceleration * (0.5 * dt * dt);
    TrueState provisional_state = current;
    provisional_state.position_meters = provisional_position;
    provisional_state.velocity_m_per_s = current.velocity_m_per_s + acceleration * dt;
    provisional_state.mass_kg = next_mass;
    const Vector3 next_central_gravity_acceleration =
        gravity_model_.acceleration(primary_body_, provisional_position);
    const Vector3 next_perturbation_acceleration =
        perturbation_model_.acceleration(primary_body_, provisional_state, clock_.time() + dt);
    const Vector3 next_environment_acceleration =
        next_central_gravity_acceleration + next_perturbation_acceleration;
    const Vector3 next_total_force =
        next_environment_acceleration * next_mass +
        main_output.thrust_force_inertial_newtons +
        rcs_output.rcs_force_inertial_newtons;
    const Vector3 next_acceleration =
        dynamics_.acceleration_from_force(next_total_force, next_mass);

    TrueState next = translational_integrator_.integrate(
        current,
        acceleration,
        next_acceleration,
        dt);

    const AttitudeState current_attitude{
        current.attitude_body_to_inertial,
        current.angular_velocity_rad_s,
        current.angular_acceleration_rad_s2};

    const AttitudeState next_attitude = attitude_integrator_.integrate(
        current_attitude,
        last_propulsion_output_.torque_body_nm,
        spacecraft_.inertia(),
        dt);

    next.attitude_body_to_inertial = next_attitude.orientation_body_to_inertial;
    next.angular_velocity_rad_s = next_attitude.angular_velocity_rad_s;
    next.angular_acceleration_rad_s2 = next_attitude.angular_acceleration_rad_s2;
    next.mass_kg = current.mass_kg - last_propulsion_output_.fuel_consumed_kg;

    spacecraft_.set_state(next);
    clock_.advance(dt);

    const Vector3 updated_gravity_acceleration =
        gravity_model_.acceleration(primary_body_, next.position_meters) +
        perturbation_model_.acceleration(primary_body_, next, next.time_seconds);
    last_sensor_data_ = sensor_suite_.measure(
        next, updated_gravity_acceleration, primary_body_.radius());
}

const SimulationClock& SimulationEngine::clock() const noexcept { return clock_; }

const CelestialBody& SimulationEngine::primary_body() const noexcept { return primary_body_; }

const Spacecraft& SimulationEngine::spacecraft() const noexcept { return spacecraft_; }

const MainEngine& SimulationEngine::main_engine() const noexcept { return main_engine_; }

const RcsModule& SimulationEngine::rcs_module() const noexcept { return rcs_module_; }

const PropulsionOutput& SimulationEngine::last_propulsion_output() const noexcept {
    return last_propulsion_output_;
}

const SensorSuite& SimulationEngine::sensor_suite() const noexcept {
    return sensor_suite_;
}

const PerturbationModel& SimulationEngine::perturbation_model() const noexcept {
    return perturbation_model_;
}

const SensorData& SimulationEngine::last_sensor_data() const noexcept {
    return last_sensor_data_;
}

} // namespace trishula
