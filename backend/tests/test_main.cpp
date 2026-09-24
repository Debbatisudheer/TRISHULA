#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "trishula/core/simulation_engine.h"
#include "trishula/sensors/sensors.h"
#include "trishula/maneuver/orbital_maneuver.h"
#include "trishula/maneuver/recovery_maneuver_executor.h"
#include "trishula/maneuver/maneuver_performance.h"
#include "trishula/maneuver/finite_burn_predictor.h"
#include "trishula/vehicle/spacecraft.h"
#include "trishula/physics/perturbations.h"

namespace {
constexpr double kMu = 3.986004418e14;
constexpr double kEarthRadius = 6.371e6;
constexpr double kMass = 1000.0;
constexpr double kDryMass = 800.0;
constexpr double kThrust = 20000.0;
constexpr double kIsp = 300.0;
constexpr double kStep = 0.1;

struct Scenario { double thrust_scale; double burn_scale; double initial_dv; };

double run(const Scenario& s) {
    const double r1 = kEarthRadius + 400e3;
    const double r2 = kEarthRadius + 500e3;
    trishula::TrueState state{};
    state.position_meters = {r1, 0.0, 0.0};
    state.velocity_m_per_s = {0.0, std::sqrt(kMu/r1) + s.initial_dv, 0.0};
    state.mass_kg = kMass;

    const trishula::Spacecraft spacecraft(state, {100.0,100.0,100.0});
    const trishula::CelestialBody earth(kMu, kEarthRadius);
    const trishula::MainEngine engine(kThrust*s.thrust_scale, kIsp, kDryMass);
    const trishula::RcsModule rcs(20.0,120.0,kDryMass);
    trishula::SensorConfiguration sensor_configuration{};
    sensor_configuration.imu.accelerometer_noise_std_m_per_s2 = 0.0005;
    sensor_configuration.imu.gyroscope_noise_std_rad_s = 2.0e-6;
    sensor_configuration.altimeter.noise_std_meters = 0.5;
    sensor_configuration.velocity.noise_std_m_per_s = 0.005;
    sensor_configuration.random_seed = 20260915;
    trishula::SimulationEngine sim(earth, spacecraft, kStep, engine, rcs,
                                   trishula::SensorSuite(sensor_configuration));
    const trishula::HohmannTransferPlanner planner;
    auto plan = planner.plan(r1,r2,kMu,kMass,kDryMass,kThrust,kIsp);
    plan.first_burn_duration_seconds *= s.burn_scale;
    plan.second_burn_duration_seconds *= s.burn_scale;
    trishula::EstimatorConfiguration estimator_configuration{};
    estimator_configuration.enable_covariance_filter = true;
    estimator_configuration.initial_position_std_meters = 100.0;
    estimator_configuration.initial_velocity_std_m_per_s = 1.0;
    estimator_configuration.process_acceleration_noise_std_m_per_s2 = 0.05;
    estimator_configuration.altitude_measurement_noise_std_meters = 0.5;
    estimator_configuration.velocity_measurement_noise_std_m_per_s = 0.005;
    estimator_configuration.max_position_correction_meters = 500.0;
    const trishula::RecoveryManeuverExecutor executor(kStep, estimator_configuration);
    const auto result = executor.execute(sim, plan);
    if (!result.apoapsis_event_detected) throw std::runtime_error("No apoapsis detected");
    if (!std::isfinite(result.final_orbit.altitude_meters)) throw std::runtime_error("Non-finite altitude");
    if (!std::isfinite(result.final_position_estimation_error_m)) throw std::runtime_error("Non-finite estimator position error");
    return result.final_orbit.altitude_meters - 500e3;
}

void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }


void test_perturbation_models() {
    const trishula::CelestialBody earth(kMu, kEarthRadius);
    trishula::TrueState state{};
    const double radius = kEarthRadius + 400e3;
    state.position_meters = {radius, 0.0, 0.0};
    state.velocity_m_per_s = {0.0, std::sqrt(kMu / radius), 0.0};
    state.mass_kg = kMass;

    trishula::PerturbationConfiguration j2_cfg{};
    j2_cfg.enable_j2 = true;
    const trishula::PerturbationModel j2_model(j2_cfg);
    require(j2_model.j2_acceleration(earth, state.position_meters).magnitude() > 0.0,
            "J2 perturbation should produce non-zero acceleration");

    trishula::PerturbationConfiguration drag_cfg{};
    drag_cfg.enable_atmospheric_drag = true;
    const trishula::PerturbationModel drag_model(drag_cfg);
    require(drag_model.atmospheric_drag_acceleration(earth, state).dot(state.velocity_m_per_s) < 0.0,
            "Atmospheric drag should oppose velocity");

    trishula::PerturbationConfiguration moon_cfg{};
    moon_cfg.enable_third_body_moon = true;
    const trishula::PerturbationModel moon_model(moon_cfg);
    require(moon_model.third_body_moon_acceleration(earth, state.position_meters, 0.0).magnitude() > 0.0,
            "Third-body lunar perturbation should be non-zero");

    trishula::PerturbationConfiguration srp_cfg{};
    srp_cfg.enable_solar_radiation_pressure = true;
    const trishula::PerturbationModel srp_model(srp_cfg);
    require(srp_model.solar_radiation_pressure_acceleration(state, 0.0).magnitude() > 0.0,
            "SRP perturbation should be non-zero");

    trishula::SimulationEngine baseline(
        earth,
        trishula::Spacecraft(state, {100.0, 100.0, 100.0}),
        kStep,
        trishula::MainEngine(kThrust, kIsp, kDryMass),
        trishula::RcsModule(20.0, 120.0, kDryMass));
    trishula::SimulationEngine perturbed(
        earth,
        trishula::Spacecraft(state, {100.0, 100.0, 100.0}),
        kStep,
        trishula::MainEngine(kThrust, kIsp, kDryMass),
        trishula::RcsModule(20.0, 120.0, kDryMass),
        trishula::SensorSuite{},
        j2_cfg);
    baseline.step_for_duration({}, 60.0);
    perturbed.step_for_duration({}, 60.0);
    require((perturbed.spacecraft().state().velocity_m_per_s -
             baseline.spacecraft().state().velocity_m_per_s).magnitude() > 0.0,
            "Enabled J2 should change the propagated trajectory");
}


void test_actuator_impulse_duration_invariant() {
    const double r = kEarthRadius + 400e3;
    const double circular_speed = std::sqrt(kMu / r);
    trishula::EstimatedState pre{};
    pre.position_meters = {r, 0.0, 0.0};
    pre.velocity_m_per_s = {0.0, circular_speed, 0.0};

    const double duration = 1.428 * 1.02;
    const double nominal_mdot = kThrust / (kIsp * 9.80665);
    const double nominal_final_mass = kMass - nominal_mdot * duration;
    const double nominal_dv = kIsp * 9.80665 * std::log(kMass / nominal_final_mass);

    trishula::EstimatedState post = pre;
    post.position_meters = {r, 100.0, 0.0};
    post.velocity_m_per_s = pre.velocity_m_per_s +
        trishula::Vector3{-kMu / (r * r), 0.0, 0.0} * duration +
        trishula::Vector3{0.0, nominal_dv, 0.0};

    const double actual_final_mass = nominal_final_mass;
    const trishula::ManeuverPerformanceEstimator estimator(kThrust, kIsp, kMu);
    const auto estimate = estimator.estimate_from_actuator_measurement(
        pre, post, duration, 1, kMass, actual_final_mass,
        nominal_dv, 1.428);
    require(estimate.valid, "Actuator impulse estimate should be valid");
    require(std::abs(estimate.effective_thrust_scale - 1.0) < 1e-9,
            "Duration scaling must not appear as thrust scaling");
    require(std::abs(estimate.duration_scale - 1.02) < 1e-12,
            "Duration scale reconstruction is incorrect");
}

void test_actuator_impulse_thrust_scale() {
    const double duration = 1.428;
    const double thrust_scale = 0.95;
    const double mdot = (kThrust * thrust_scale) / (kIsp * 9.80665);
    const double final_mass = kMass - mdot * duration;
    const double actual_dv = kIsp * 9.80665 * std::log(kMass / final_mass);

    trishula::EstimatedState pre{};
    pre.position_meters = {kEarthRadius + 400e3, 0.0, 0.0};
    pre.velocity_m_per_s = {0.0, std::sqrt(kMu / (kEarthRadius + 400e3)), 0.0};
    trishula::EstimatedState post = pre;

    const trishula::ManeuverPerformanceEstimator estimator(kThrust, kIsp, kMu);
    const auto estimate = estimator.estimate_from_actuator_measurement(
        pre, post, duration, 1, kMass, final_mass, actual_dv, duration);
    require(estimate.valid, "Thrust scale estimate should be valid");
    require(std::abs(estimate.effective_thrust_scale - thrust_scale) < 5e-4,
            "Measured actuator impulse did not recover thrust scale");
}


} // namespace


void test_predictor_inherits_active_perturbations() {
    const double radius = kEarthRadius + 400e3;
    const double speed = std::sqrt(kMu / radius);
    trishula::TrueState state{};
    state.position_meters = {radius, 0.0, 0.0};
    state.velocity_m_per_s = {0.0, speed, 0.0};
    state.mass_kg = kMass;
    const trishula::Spacecraft spacecraft(state, {100.0, 100.0, 100.0});
    const trishula::CelestialBody earth(kMu, kEarthRadius);
    const trishula::MainEngine engine(kThrust, kIsp, kDryMass);
    const trishula::RcsModule rcs(20.0, 120.0, kDryMass);

    trishula::SimulationEngine baseline(
        earth, spacecraft, kStep, engine, rcs, trishula::SensorSuite{});
    trishula::PerturbationConfiguration strong_j2{};
    strong_j2.enable_j2 = true;
    strong_j2.j2 = 0.10; // intentionally exaggerated so this regression is numerically obvious.
    trishula::SimulationEngine perturbed(
        earth, spacecraft, kStep, engine, rcs, trishula::SensorSuite{}, strong_j2);

    trishula::EstimatorConfiguration estimator_cfg{};
    estimator_cfg.enable_covariance_filter = false;
    trishula::StateEstimator baseline_estimator(state.position_meters, state.velocity_m_per_s,
                                                 state.attitude_body_to_inertial, estimator_cfg);
    trishula::StateEstimator perturbed_estimator(state.position_meters, state.velocity_m_per_s,
                                                  state.attitude_body_to_inertial, estimator_cfg);

    const double target_a = radius + 75e3;
    trishula::FiniteBurnPredictor predictor(kStep);
    const auto baseline_solution = predictor.solve(baseline, baseline_estimator, target_a, 100.0);
    const auto perturbed_solution = predictor.solve(perturbed, perturbed_estimator, target_a, 100.0);

    require(baseline_solution.valid && perturbed_solution.valid,
            "Finite-burn predictor should solve both baseline and perturbed cases");
    require(std::abs(baseline_solution.delta_v_m_per_s - perturbed_solution.delta_v_m_per_s) > 1.0e-6,
            "Finite-burn predictor ignored the active perturbation configuration");
}

void test_exact_fractional_burn_duration() {
    const double r = kEarthRadius + 400e3;
    trishula::TrueState state{};
    state.position_meters = {r, 0.0, 0.0};
    state.velocity_m_per_s = {0.0, std::sqrt(kMu / r), 0.0};
    state.mass_kg = kMass;
    const trishula::Spacecraft spacecraft(state, {100.0, 100.0, 100.0});
    const trishula::CelestialBody earth(kMu, kEarthRadius);
    const trishula::MainEngine engine(kThrust, kIsp, kDryMass);
    const trishula::RcsModule rcs(20.0, 120.0, kDryMass);
    trishula::SimulationEngine simulation(earth, spacecraft, kStep, engine, rcs);
    trishula::PropulsionCommand command{};
    command.main_engine_throttle = 1.0;
    command.commanded_thrust_direction_body = {0.0, 1.0, 0.0};
    const double dt = 0.037;
    simulation.step_for_duration(command, dt);
    require(std::abs(simulation.clock().time() - dt) < 1e-12,
            "Fractional simulation step did not advance exact duration");
    require(std::abs(simulation.spacecraft().state().time_seconds - dt) < 1e-12,
            "Spacecraft time did not advance exact fractional duration");
}

void test_perturbation_aware_coast_targeting() {
    const double radius = kEarthRadius + 400e3;
    const double speed = std::sqrt(kMu / radius);
    trishula::TrueState state{};
    state.position_meters = {radius, 0.0, 0.0};
    state.velocity_m_per_s = {0.0, speed, 0.0};
    state.mass_kg = kMass;
    const trishula::CelestialBody earth(kMu, kEarthRadius);
    const trishula::MainEngine engine(kThrust, kIsp, kDryMass);
    const trishula::RcsModule rcs(20.0, 120.0, kDryMass);
    trishula::PerturbationConfiguration cfg{};
    cfg.enable_j2 = true;
    cfg.j2 = 0.10;
    trishula::SimulationEngine perturbed(
        earth, trishula::Spacecraft(state, {100.0, 100.0, 100.0}), kStep, engine, rcs,
        trishula::SensorSuite{}, cfg);
    trishula::EstimatorConfiguration estimator_cfg{};
    estimator_cfg.enable_covariance_filter = false;
    trishula::StateEstimator estimator(state.position_meters, state.velocity_m_per_s,
                                       state.attitude_body_to_inertial, estimator_cfg);
    trishula::FiniteBurnPredictor predictor(kStep);
    const double target_periapsis = radius - 50e3;
    const auto instantaneous = predictor.solve_for_periapsis(
        perturbed, estimator, target_periapsis, 100.0);
    const auto coast_aware = predictor.solve_for_periapsis_after_coast(
        perturbed, estimator, target_periapsis, 100.0, 8000.0);
    require(instantaneous.valid && coast_aware.valid,
            "Perturbation-aware periapsis targeting should solve");
    require(std::abs(instantaneous.delta_v_m_per_s - coast_aware.delta_v_m_per_s) > 1.0e-8,
            "Coast-aware targeting should account for perturbed coast dynamics");
}


int main() {
    try {
        test_actuator_impulse_duration_invariant();
        test_exact_fractional_burn_duration();
        test_perturbation_models();
        test_perturbation_aware_coast_targeting();
        test_predictor_inherits_active_perturbations();
        test_actuator_impulse_thrust_scale();
        const std::vector<Scenario> cases{
            {1.0,1.0,0.0},{1.0,1.0,1.0},{1.0,1.0,-1.0},{0.95,1.0,0.0},{1.0,1.02,0.0},{1.0,0.98,0.0}
        };
        std::vector<double> errors;
        for (const auto& c : cases) errors.push_back(run(c));
        require(std::abs(errors[0]) < 100.0, "Nominal covariance-filter recovery accuracy regression");
        for (double e : errors) require(std::isfinite(e), "Recovery produced non-finite error");
        std::cout << "V0.9.22 perturbation-aware coast targeting and recovery tests passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILURE: " << e.what() << '\n';
        return 1;
    }
}
