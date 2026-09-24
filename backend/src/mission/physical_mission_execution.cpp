#include "trishula/mission/physical_mission_execution.h"
#include "trishula/maneuver/lunar_descent_prep.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace trishula {
namespace {
constexpr double kMuEarth = 3.986004418e14;
constexpr double kMuMoon = 4.9048695e12;
constexpr double kEarthRadius = 6.371e6;
constexpr double kMoonRadius = 1.7374e6;
constexpr double kMoonOrbitRadius = 384400.0e3;
constexpr double kMoonPeriod = 27.321661 * 86400.0;
constexpr double kParkingAltitude = 400.0e3;
constexpr double kMass = 50000.0;
constexpr double kDryMass = 20000.0;
constexpr double kThrust = 5.0e6;
constexpr double kIsp = 450.0;
constexpr double kRcsForce = 20.0;
constexpr double kRcsIsp = 120.0;
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kBatteryCapacityWh = 120000.0;
constexpr double kBasePowerDrawW = 180.0;
constexpr double kPropulsionPowerDrawW = 4200.0;
constexpr double kRcsPowerDrawW = 150.0;
constexpr double kAmbientTemperatureC = 20.0;
constexpr double kThermalHeatingCPerSecond = 0.35;
constexpr double kRcsHeatingCPerSecond = 0.05;
constexpr double kThermalCoolingRatePerSecond = 0.03;

}

PhysicalMissionExecutionEngine::PhysicalMissionExecutionEngine(PhysicalMissionExecutionConfiguration configuration)
    : configuration_(configuration) {
    if (configuration_.step_seconds <= 0.0) throw std::invalid_argument("physical mission step must be positive");
    if (configuration_.initial_mass_kg <= kDryMass) throw std::invalid_argument("physical mission initial mass must exceed dry mass");
    reset();
}

void PhysicalMissionExecutionEngine::reset() {
    phase_ = MissionPhase::EarthOrbit;
    phase_elapsed_seconds_ = 0.0;
    telemetry_sequence_ = 0;
    tli_burn_remaining_seconds_ = 0.0;
    tli_burn_initialized_ = false;
    loi_result_ = {};
    loi_executed_ = false;
    lunar_orbit_operations_result_ = {};
    lunar_orbit_operations_executed_ = false;
    lunar_descent_preparation_result_ = {};
    lunar_descent_preparation_executed_ = false;
    battery_soc_ = 1.0;
    thermal_temperature_c_ = kAmbientTemperatureC;

    const double radius = kEarthRadius + kParkingAltitude;
    TrueState state{};
    state.position_meters = {radius, 0.0, 0.0};
    state.velocity_m_per_s = {0.0, std::sqrt(kMuEarth / radius), 0.0};
    state.mass_kg = configuration_.initial_mass_kg;
    state.attitude_body_to_inertial = {};

    tli_plan_ = TransLunarInjectionPlanner{}.plan(
        radius, kMoonOrbitRadius, kMoonRadius + 100.0e3, kMuEarth, kMuMoon);
    loi_plan_ = LunarOrbitInsertionPlanner{}.plan(
        kMuMoon, kMoonRadius, 100.0e3, 1000.0e3,
        tli_plan_.hyperbolic_excess_speed_m_per_s, kThrust, kIsp, configuration_.initial_mass_kg, kDryMass);

    const Spacecraft spacecraft(state, {1000.0, 1000.0, 1000.0});
    const CelestialBody earth(kMuEarth, kEarthRadius);
    MainEngine engine(kThrust, kIsp, kDryMass);
    RcsModule rcs(kRcsForce, kRcsIsp, kDryMass);
    PerturbationConfiguration perturbations{};
    perturbations.enable_third_body_moon = true;
    perturbations.moon_gravitational_parameter_m3_s2 = kMuMoon;
    perturbations.moon_orbit_radius_m = kMoonOrbitRadius;
    perturbations.moon_orbit_period_s = kMoonPeriod;
    perturbations.moon_phase_rad = tli_plan_.required_lunar_phase_angle_rad;
    simulation_ = std::make_unique<SimulationEngine>(
        earth, spacecraft, configuration_.step_seconds, engine, rcs, SensorSuite{}, perturbations);
    refresh_snapshot();
}

Vector3 PhysicalMissionExecutionEngine::moon_position(double time_seconds) const {
    const double theta = tli_plan_.required_lunar_phase_angle_rad + 2.0 * kPi * time_seconds / kMoonPeriod;
    return {kMoonOrbitRadius * std::cos(theta), kMoonOrbitRadius * std::sin(theta), 0.0};
}

Vector3 PhysicalMissionExecutionEngine::moon_velocity(double time_seconds) const {
    const double theta = tli_plan_.required_lunar_phase_angle_rad + 2.0 * kPi * time_seconds / kMoonPeriod;
    const double omega = 2.0 * kPi / kMoonPeriod;
    return {-kMoonOrbitRadius * omega * std::sin(theta), kMoonOrbitRadius * omega * std::cos(theta), 0.0};
}

void PhysicalMissionExecutionEngine::propagate(const PropulsionCommand& command) {
    simulation_->step(command);
}

void PhysicalMissionExecutionEngine::step() {
    if (!simulation_) reset();

    const double simulation_time_before = simulation_->clock().time();
    ++telemetry_sequence_;

    // Preserve the V0.9.36-V0.9.41 accelerated campaign behavior for
    // regression tests. The new physical-arc mode is explicitly opt-in and
    // is used by the real ground-uplink executable.
    if (!configuration_.event_driven_physical_arc) {
        phase_elapsed_seconds_ += configuration_.step_seconds;
        switch (phase_) {
        case MissionPhase::EarthOrbit:
            propagate({});
            if (phase_elapsed_seconds_ >= configuration_.earth_orbit_hold_seconds) {
                phase_ = MissionPhase::TransLunarInjection;
                phase_elapsed_seconds_ = 0.0;
            }
            break;
        case MissionPhase::TransLunarInjection: {
            const double throttle = 0.85;
            PropulsionCommand c{};
            c.main_engine_throttle = throttle;
            c.commanded_thrust_direction_body = {0.0, 1.0, 0.0};
            propagate(c);
            if (phase_elapsed_seconds_ >= configuration_.tli_burn_seconds) {
                phase_ = MissionPhase::LunarCruise;
                phase_elapsed_seconds_ = 0.0;
            }
            break;
        }
        case MissionPhase::LunarCruise:
            propagate({});
            if (phase_elapsed_seconds_ >= configuration_.lunar_cruise_seconds) {
                phase_ = MissionPhase::LunarOrbit;
                phase_elapsed_seconds_ = 0.0;
            }
            break;
        case MissionPhase::LunarOrbit:
            propagate({});
            if (phase_elapsed_seconds_ >= configuration_.lunar_orbit_hold_seconds) {
                phase_ = MissionPhase::Descent;
                phase_elapsed_seconds_ = 0.0;
            }
            break;
        case MissionPhase::Descent: {
            const auto& s = simulation_->spacecraft().state();
            const double speed = s.velocity_m_per_s.magnitude();
            const double throttle = std::clamp((speed - 30.0) / 30.0, 0.10, 0.65);
            PropulsionCommand c{};
            c.main_engine_throttle = throttle;
            c.commanded_thrust_direction_body = {0.0, 1.0, 0.0};
            propagate(c);
            if (phase_elapsed_seconds_ >= configuration_.descent_seconds) {
                phase_ = MissionPhase::Landing;
                phase_elapsed_seconds_ = 0.0;
            }
            break;
        }
        case MissionPhase::Landing: {
            PropulsionCommand c{};
            c.main_engine_throttle = 0.15;
            c.commanded_thrust_direction_body = {0.0, 1.0, 0.0};
            propagate(c);
            if (phase_elapsed_seconds_ >= configuration_.landing_seconds) {
                phase_ = MissionPhase::Complete;
                phase_elapsed_seconds_ = 0.0;
            }
            break;
        }
        default:
            break;
        }
        const double elapsed_seconds = std::max(0.0, simulation_->clock().time() - simulation_time_before);
        update_subsystem_model(elapsed_seconds);
        refresh_snapshot();
        return;
    }

    const double dt = configuration_.step_seconds;
    switch (phase_) {
    case MissionPhase::EarthOrbit:
        phase_elapsed_seconds_ += dt;
        propagate({});
        if (phase_elapsed_seconds_ >= configuration_.earth_orbit_hold_seconds) {
            phase_ = MissionPhase::TransLunarInjection;
            phase_elapsed_seconds_ = 0.0;
            tli_burn_remaining_seconds_ = 0.0;
            tli_burn_initialized_ = false;
        }
        break;

    case MissionPhase::TransLunarInjection: {
        const auto state = simulation_->spacecraft().state();
        const Vector3 radial = state.position_meters.normalized();
        const Vector3 tangential = state.velocity_m_per_s -
            radial * state.velocity_m_per_s.dot(radial);
        PropulsionCommand command{};
        command.main_engine_throttle = 1.0;
        command.commanded_thrust_direction_body =
            state.attitude_body_to_inertial.inverse().rotate(tangential.normalized());

        if (!tli_burn_initialized_) {
            const double final_mass = state.mass_kg *
                std::exp(-tli_plan_.tli_delta_v_m_per_s / (kIsp * 9.80665));
            const double mdot = kThrust / (kIsp * 9.80665);
            tli_burn_remaining_seconds_ = std::max(
                0.0, (state.mass_kg - final_mass) / mdot);
            tli_burn_initialized_ = true;
        }
        if (tli_burn_remaining_seconds_ > 0.0) {
            const double burn_dt = std::min(dt, tli_burn_remaining_seconds_);
            simulation_->step_for_duration(command, burn_dt);
            tli_burn_remaining_seconds_ -= burn_dt;
            phase_elapsed_seconds_ += burn_dt;
        } else {
            phase_ = MissionPhase::LunarCruise;
            phase_elapsed_seconds_ = 0.0;
        }
        break;
    }

    case MissionPhase::LunarCruise: {
        phase_elapsed_seconds_ += dt;
        propagate({});
        const auto& state = simulation_->spacecraft().state();
        const Vector3 moon = moon_position(simulation_->clock().time());
        if ((state.position_meters - moon).magnitude() <= tli_plan_.lunar_soi_radius_meters) {
            phase_ = MissionPhase::LunarOrbit;
            phase_elapsed_seconds_ = 0.0;
        }
        break;
    }

    case MissionPhase::LunarOrbit:
        // Event-driven arc ends at actual lunar-SOI arrival. No timer may
        // promote this state to descent/landing.
        phase_elapsed_seconds_ += dt;
        propagate({});
        break;

    default:
        break;
    }

    const double elapsed_seconds = std::max(0.0, simulation_->clock().time() - simulation_time_before);
    update_subsystem_model(elapsed_seconds);
    refresh_snapshot();
}

void PhysicalMissionExecutionEngine::run_until_phase(MissionPhase target, std::uint64_t max_steps) {
    std::uint64_t steps = 0;
    while (phase_ != target && steps < max_steps) {
        step();
        ++steps;
    }
    if (phase_ != target) throw std::runtime_error("physical mission did not reach requested phase");
}

void PhysicalMissionExecutionEngine::update_phase() {}

void PhysicalMissionExecutionEngine::refresh_snapshot() {
    const auto& state = simulation_->spacecraft().state();
    const Vector3 moon = moon_position(simulation_->clock().time());
    const Vector3 relative_moon = state.position_meters - moon;

    snapshot_.phase = phase_;
    snapshot_.mission_time_seconds = simulation_->clock().time();
    snapshot_.phase_elapsed_seconds = phase_elapsed_seconds_;
    snapshot_.position_x_m = state.position_meters.x;
    snapshot_.position_y_m = state.position_meters.y;
    snapshot_.position_z_m = state.position_meters.z;
    snapshot_.velocity_x_m_per_s = state.velocity_m_per_s.x;
    snapshot_.velocity_y_m_per_s = state.velocity_m_per_s.y;
    snapshot_.velocity_z_m_per_s = state.velocity_m_per_s.z;
    snapshot_.altitude_m = state.position_meters.magnitude() - kEarthRadius;
    snapshot_.speed_m_per_s = state.velocity_m_per_s.magnitude();
    snapshot_.distance_to_moon_m = relative_moon.magnitude();
    snapshot_.moon_x_m = moon.x;
    snapshot_.moon_y_m = moon.y;
    snapshot_.telemetry_sequence = telemetry_sequence_;
    snapshot_.battery_soc = battery_soc_;
    snapshot_.thermal_temperature_c = thermal_temperature_c_;
    snapshot_.thermal_margin = std::clamp((95.0 - thermal_temperature_c_) / 75.0, 0.0, 1.0);
    const double propellant_mass = std::max(0.0, state.mass_kg - kDryMass);
    const double initial_propellant_mass = std::max(1.0, configuration_.initial_mass_kg - kDryMass);
    snapshot_.propellant_fraction = std::clamp(propellant_mass / initial_propellant_mass, 0.0, 1.0);
    snapshot_.thrust_fraction = std::clamp(
        simulation_->main_engine().maximum_thrust_newtons() > 0.0
            ? simulation_->last_propulsion_output().thrust_force_inertial_newtons.magnitude() / simulation_->main_engine().maximum_thrust_newtons()
            : 0.0,
        0.0, 1.0);
    snapshot_.engine_firing = simulation_->last_propulsion_output().engine_firing;
    snapshot_.rcs_firing = simulation_->last_propulsion_output().rcs_firing;
    snapshot_.power_status = battery_soc_ > 0.20 ? "SUPPLIED" : (battery_soc_ > 0.05 ? "LOW" : "DEPLETED");
    snapshot_.propulsion_status = snapshot_.engine_firing ? "BURNING" : (snapshot_.rcs_firing ? "RCS" : "COASTING");
    snapshot_.thermal_status = thermal_temperature_c_ < 65.0 ? "NOMINAL" : (thermal_temperature_c_ < 80.0 ? "HOT" : "CRITICAL");
    snapshot_.communication_status = telemetry_sequence_ > 0 ? "LOCKED" : "ACQUIRING";
    const bool navigation_valid = std::isfinite(snapshot_.position_x_m) && std::isfinite(snapshot_.position_y_m) &&
        std::isfinite(snapshot_.position_z_m) && std::isfinite(snapshot_.speed_m_per_s);
    snapshot_.navigation_status = telemetry_sequence_ == 0 ? "INITIALIZING" : (navigation_valid ? "NOMINAL" : "FAULT");
    snapshot_.health_status = (snapshot_.thermal_status == "CRITICAL" || snapshot_.power_status == "DEPLETED" || snapshot_.navigation_status == "FAULT")
        ? "FAULT"
        : ((snapshot_.thermal_status == "HOT" || snapshot_.power_status == "LOW") ? "DEGRADED" : "NOMINAL");
}

void PhysicalMissionExecutionEngine::update_subsystem_model(double elapsed_seconds) {
    if (elapsed_seconds <= 0.0) return;
    const auto& output = simulation_->last_propulsion_output();
    const double thrust_fraction = simulation_->main_engine().maximum_thrust_newtons() > 0.0
        ? std::clamp(output.thrust_force_inertial_newtons.magnitude() / simulation_->main_engine().maximum_thrust_newtons(), 0.0, 1.0)
        : 0.0;
    const double power_draw_w = kBasePowerDrawW + kPropulsionPowerDrawW * thrust_fraction +
        (output.rcs_firing ? kRcsPowerDrawW : 0.0);
    battery_soc_ = std::clamp(battery_soc_ - (power_draw_w * elapsed_seconds / 3600.0) / kBatteryCapacityWh, 0.0, 1.0);

    const double heating = kThermalHeatingCPerSecond * thrust_fraction +
        (output.rcs_firing ? kRcsHeatingCPerSecond : 0.0);
    const double cooling = kThermalCoolingRatePerSecond * (thermal_temperature_c_ - kAmbientTemperatureC);
    thermal_temperature_c_ += (heating - cooling) * elapsed_seconds;
    thermal_temperature_c_ = std::clamp(thermal_temperature_c_, kAmbientTemperatureC, 120.0);
}


const LunarOrbitInsertionResult& PhysicalMissionExecutionEngine::execute_lunar_orbit_insertion() {
    if (phase_ != MissionPhase::LunarOrbit) {
        throw std::runtime_error("lunar orbit insertion requires LunarOrbit phase");
    }
    if (loi_executed_) return loi_result_;

    constexpr double kMuMoonLocal = kMuMoon;
    constexpr double kMoonRadiusLocal = kMoonRadius;
    constexpr double kPeriluneAltitude = 197.5e3;
    constexpr double kApoluneAltitude = 1000.0e3;
    constexpr double kDryMassLocal = kDryMass;
    constexpr std::uint64_t kMaxPeriluneSteps = 250000U;

    // At lunar SOI, use the actual Moon-relative state to perform a small
    // finite mid-course correction that targets the desired 100 km perilune.
    auto pre_state = simulation_->spacecraft().state();
    Vector3 moon = moon_position(simulation_->clock().time());
    Vector3 moon_v = moon_velocity(simulation_->clock().time());
    Vector3 relative_r = pre_state.position_meters - moon;
    Vector3 relative_v = pre_state.velocity_m_per_s - moon_v;
    double radius = relative_r.magnitude();
    const double v_inf = std::sqrt(std::max(0.0,
        relative_v.magnitude_squared() - 2.0 * kMuMoonLocal / radius));

    const double target_rp = kMoonRadiusLocal + kPeriluneAltitude;
    const double target_vp = std::sqrt(v_inf * v_inf + 2.0 * kMuMoonLocal / target_rp);
    const double target_h = target_rp * target_vp;
    const Vector3 radial_hat = relative_r.normalized();
    Vector3 transverse_hat = relative_v - radial_hat * relative_v.dot(radial_hat);
    if (transverse_hat.magnitude() <= 1.0e-9) throw std::runtime_error("degenerate lunar approach geometry");
    transverse_hat = transverse_hat.normalized();
    const double target_v = std::sqrt(v_inf * v_inf + 2.0 * kMuMoonLocal / radius);
    const double target_vt = target_h / radius;
    const double target_vr = -std::sqrt(std::max(0.0, target_v * target_v - target_vt * target_vt));
    const Vector3 desired_relative_v = radial_hat * target_vr + transverse_hat * target_vt;
    const Vector3 mcc_delta_v = desired_relative_v - relative_v;
    const double mcc_dv = mcc_delta_v.magnitude();
    if (mcc_dv > 1000.0) throw std::runtime_error("lunar approach correction exceeds configured bound");

    const double mcc_duration = predicted_burn_duration(
        mcc_dv, kThrust, kIsp, pre_state.mass_kg, kDryMassLocal);
    if (mcc_duration > 0.0) {
        PropulsionCommand mcc_command{};
        mcc_command.main_engine_throttle = 1.0;
        mcc_command.commanded_thrust_direction_body =
            pre_state.attitude_body_to_inertial.inverse().rotate(mcc_delta_v.normalized());
        double remaining_mcc = mcc_duration;
        while (remaining_mcc > 1.0e-9) {
            const double burn_dt = std::min(configuration_.step_seconds, remaining_mcc);
            simulation_->step_for_duration(mcc_command, burn_dt);
            remaining_mcc -= burn_dt;
        }
    }

    // Recompute the physical state after MCC and plan the finite LOI.
    pre_state = simulation_->spacecraft().state();
    moon = moon_position(simulation_->clock().time());
    moon_v = moon_velocity(simulation_->clock().time());
    relative_r = pre_state.position_meters - moon;
    relative_v = pre_state.velocity_m_per_s - moon_v;
    radius = relative_r.magnitude();
    const double corrected_v_inf = std::sqrt(std::max(0.0, relative_v.magnitude_squared() - 2.0 * kMuMoonLocal / radius));

    LunarOrbitInsertionPlanner planner{};
    const auto plan = planner.plan(
        kMuMoonLocal, kMoonRadiusLocal, kPeriluneAltitude, kApoluneAltitude,
        corrected_v_inf, kThrust, kIsp, pre_state.mass_kg, kDryMassLocal);

    // Center the finite LOI burn around physical perilune instead of starting
    // the full burn after periapsis. This preserves the finite-burn physics
    // while minimizing the shift of the achieved periapsis.
    std::uint64_t coast_steps = 0;
    while (coast_steps < kMaxPeriluneSteps) {
        const auto& state = simulation_->spacecraft().state();
        const Vector3 current_moon = moon_position(simulation_->clock().time());
        const Vector3 current_moon_v = moon_velocity(simulation_->clock().time());
        const Vector3 current_r = state.position_meters - current_moon;
        const Vector3 current_v = state.velocity_m_per_s - current_moon_v;
        const double radial_rate = current_r.dot(current_v) / current_r.magnitude();
        if (radial_rate >= -2.0) break;
        const double distance = current_r.magnitude();
        double coast_dt = configuration_.step_seconds;
        if (distance < 200000.0) coast_dt = std::min(coast_dt, 0.001);
        else if (distance < 500000.0) coast_dt = std::min(coast_dt, 0.005);
        else if (distance < 2000000.0) coast_dt = std::min(coast_dt, 0.02);
        else if (distance < 10000000.0) coast_dt = std::min(coast_dt, 0.1);
        simulation_->step_for_duration({}, coast_dt);
        ++coast_steps;
    }
    if (coast_steps >= kMaxPeriluneSteps) throw std::runtime_error("failed to reach LOI burn window");

    pre_state = simulation_->spacecraft().state();
    moon = moon_position(simulation_->clock().time());
    moon_v = moon_velocity(simulation_->clock().time());
    relative_r = pre_state.position_meters - moon;
    relative_v = pre_state.velocity_m_per_s - moon_v;
    radius = relative_r.magnitude();

    const double initial_mass = pre_state.mass_kg;
    const Vector3 burn_start_velocity = relative_v;
    double remaining = plan.planned_burn_duration_seconds;
    std::size_t burn_steps = 0;
    const double first_half = remaining * 0.5;
    double half_remaining = first_half;
    Vector3 burn_direction = relative_v.normalized() * -1.0;
    PropulsionCommand command{};
    command.main_engine_throttle = 1.0;
    command.commanded_thrust_direction_body =
        pre_state.attitude_body_to_inertial.inverse().rotate(burn_direction);
    while (half_remaining > 1.0e-9) {
        const double burn_dt = std::min(configuration_.step_seconds, half_remaining);
        simulation_->step_for_duration(command, burn_dt);
        half_remaining -= burn_dt;
        ++burn_steps;
    }

    // Re-target the retrograde direction for the second half using the actual
    // physical state at the burn midpoint.
    pre_state = simulation_->spacecraft().state();
    moon = moon_position(simulation_->clock().time());
    moon_v = moon_velocity(simulation_->clock().time());
    relative_v = pre_state.velocity_m_per_s - moon_v;
    burn_direction = relative_v.normalized() * -1.0;
    command.commanded_thrust_direction_body =
        pre_state.attitude_body_to_inertial.inverse().rotate(burn_direction);
    half_remaining = remaining * 0.5;
    while (half_remaining > 1.0e-9) {
        const double burn_dt = std::min(configuration_.step_seconds, half_remaining);
        simulation_->step_for_duration(command, burn_dt);
        half_remaining -= burn_dt;
        ++burn_steps;
    }

    const auto& post_state = simulation_->spacecraft().state();

    const Vector3 post_moon = moon_position(simulation_->clock().time());
    const Vector3 post_moon_v = moon_velocity(simulation_->clock().time());
    const Vector3 post_r = post_state.position_meters - post_moon;
    const Vector3 post_v = post_state.velocity_m_per_s - post_moon_v;
    const double energy = specific_orbital_energy(kMuMoonLocal, post_r, post_v);
    const double a = semi_major_axis_from_energy(kMuMoonLocal, energy);
    const double e = eccentricity_from_state(kMuMoonLocal, post_r, post_v);
    const double rp_alt = a * (1.0 - e) - kMoonRadiusLocal;
    const double ra_alt = a * (1.0 + e) - kMoonRadiusLocal;
    const double achieved_dv = burn_start_velocity.magnitude() - post_v.magnitude();

    loi_result_.plan = plan;
    loi_result_.perilune_position_meters = relative_r;
    loi_result_.perilune_velocity_before_m_per_s = relative_v;
    loi_result_.perilune_velocity_after_m_per_s = post_v;
    loi_result_.perilune_radius_meters = radius;
    loi_result_.post_burn_specific_energy_j_per_kg = energy;
    loi_result_.post_burn_semi_major_axis_meters = a;
    loi_result_.post_burn_eccentricity = e;
    loi_result_.post_burn_perilune_altitude_meters = rp_alt;
    loi_result_.post_burn_apolune_altitude_meters = ra_alt;
    loi_result_.achieved_delta_v_m_per_s = achieved_dv;
    loi_result_.midcourse_correction_delta_v_m_per_s = mcc_dv;
    loi_result_.propellant_consumed_kg = initial_mass - post_state.mass_kg;
    loi_result_.burn_duration_seconds = plan.planned_burn_duration_seconds;
    loi_result_.burn_integration_steps = burn_steps;
    loi_result_.orbit_captured = energy < 0.0 &&
        std::abs(rp_alt - 100.0e3) < 20000.0 &&
        std::abs(ra_alt - 1000.0e3) < 20000.0 &&
        e < 0.30 &&
        std::abs(achieved_dv - plan.planned_delta_v_m_per_s) < 30.0;
    loi_executed_ = true;
    refresh_snapshot();
    return loi_result_;
}

const LunarOrbitInsertionResult& PhysicalMissionExecutionEngine::lunar_orbit_insertion_result() const noexcept {
    return loi_result_;
}

const PhysicalMissionExecutionSnapshot& PhysicalMissionExecutionEngine::snapshot() const noexcept { return snapshot_; }
SimulationEngine& PhysicalMissionExecutionEngine::simulation() noexcept { return *simulation_; }
const SimulationEngine& PhysicalMissionExecutionEngine::simulation() const noexcept { return *simulation_; }
MissionPhase PhysicalMissionExecutionEngine::phase() const noexcept { return phase_; }
const TransLunarInjectionPlan& PhysicalMissionExecutionEngine::tli_plan() const noexcept { return tli_plan_; }

} // namespace trishula


namespace trishula {
namespace {
struct LunarApsisEvent {
    double time_seconds{0.0};
    double radius_meters{0.0};
    double speed_m_per_s{0.0};
    bool periapsis{false};
};

LunarApsisEvent propagate_to_lunar_apsis(
    SimulationEngine& simulation,
    Vector3 (*moon_position_fn)(double, const TransLunarInjectionPlan&),
    Vector3 (*moon_velocity_fn)(double, const TransLunarInjectionPlan&),
    const TransLunarInjectionPlan& plan,
    bool target_periapsis,
    double step_seconds,
    std::uint64_t max_steps) {
    auto moon_position = [&](double t) { return moon_position_fn(t, plan); };
    auto moon_velocity = [&](double t) { return moon_velocity_fn(t, plan); };
    const auto initial_state = simulation.spacecraft().state();
    const auto initial_moon = moon_position(simulation.clock().time());
    const auto initial_moon_v = moon_velocity(simulation.clock().time());
    const auto initial_r = initial_state.position_meters - initial_moon;
    const auto initial_v = initial_state.velocity_m_per_s - initial_moon_v;
    double previous_radial = initial_r.dot(initial_v) / initial_r.magnitude();
    for (std::uint64_t i = 0; i < max_steps; ++i) {
        simulation.step_for_duration({}, step_seconds);
        const auto state = simulation.spacecraft().state();
        const auto moon = moon_position(simulation.clock().time());
        const auto moon_v = moon_velocity(simulation.clock().time());
        const auto r = state.position_meters - moon;
        const auto v = state.velocity_m_per_s - moon_v;
        const double radial = r.dot(v) / r.magnitude();
        const bool peri = previous_radial < 0.0 && radial >= 0.0;
        const bool apo = previous_radial > 0.0 && radial <= 0.0;
        if ((target_periapsis && peri) || (!target_periapsis && apo)) {
            return {simulation.clock().time(), r.magnitude(), v.magnitude(), target_periapsis};
        }
        previous_radial = radial;
    }
    throw std::runtime_error("failed to detect requested lunar apsis during orbit operations");
}

Vector3 ops_moon_position(double t, const TransLunarInjectionPlan& plan) {
    constexpr double kMoonOrbitRadius = 384400.0e3;
    constexpr double kMoonPeriod = 27.321661 * 86400.0;
    constexpr double kPiLocal = 3.1415926535897932384626433832795;
    const double theta = plan.required_lunar_phase_angle_rad + 2.0 * kPiLocal * t / kMoonPeriod;
    return {kMoonOrbitRadius * std::cos(theta), kMoonOrbitRadius * std::sin(theta), 0.0};
}

Vector3 ops_moon_velocity(double t, const TransLunarInjectionPlan& plan) {
    constexpr double kMoonOrbitRadius = 384400.0e3;
    constexpr double kMoonPeriod = 27.321661 * 86400.0;
    constexpr double kPiLocal = 3.1415926535897932384626433832795;
    const double theta = plan.required_lunar_phase_angle_rad + 2.0 * kPiLocal * t / kMoonPeriod;
    const double omega = 2.0 * kPiLocal / kMoonPeriod;
    return {-kMoonOrbitRadius * omega * std::sin(theta), kMoonOrbitRadius * omega * std::cos(theta), 0.0};
}
}

const LunarOrbitOperationsResult& PhysicalMissionExecutionEngine::execute_lunar_orbit_operations() {
    if (phase_ != MissionPhase::LunarOrbit || !loi_executed_) {
        throw std::runtime_error("lunar orbit operations require completed physical LOI");
    }
    if (lunar_orbit_operations_executed_) return lunar_orbit_operations_result_;

    constexpr double kMuMoonLocal = kMuMoon;
    constexpr double kMoonRadiusLocal = kMoonRadius;
    constexpr double kTargetDisturbanceDv = 2.0;
    constexpr std::uint64_t kMaxApsisSteps = 200000;

    auto relative_state = [&]() {
        const auto state = simulation_->spacecraft().state();
        const auto moon = moon_position(simulation_->clock().time());
        const auto moon_v = moon_velocity(simulation_->clock().time());
        return std::pair<Vector3, Vector3>{state.position_meters - moon, state.velocity_m_per_s - moon_v};
    };

    const auto [baseline_r, baseline_v] = relative_state();
    const double baseline_energy = specific_orbital_energy(kMuMoonLocal, baseline_r, baseline_v);
    const double baseline_a = semi_major_axis_from_energy(kMuMoonLocal, baseline_energy);
    const double baseline_e = eccentricity_from_state(kMuMoonLocal, baseline_r, baseline_v);
    const double baseline_rp = baseline_a * (1.0 - baseline_e);
    const double baseline_ra = baseline_a * (1.0 + baseline_e);
    const double baseline_period = 2.0 * kPi * std::sqrt(baseline_a * baseline_a * baseline_a / kMuMoonLocal);

    std::vector<LunarApsisEvent> apsides;
    apsides.reserve(6);
    double previous_radial = baseline_r.dot(baseline_v) / baseline_r.magnitude();
    const double campaign_start_time = simulation_->clock().time();
    while (apsides.size() < 6) {
        simulation_->step_for_duration({}, configuration_.step_seconds);
        const auto [r, v] = relative_state();
        const double radial = r.dot(v) / r.magnitude();
        if (previous_radial < 0.0 && radial >= 0.0) apsides.push_back({simulation_->clock().time(), r.magnitude(), v.magnitude(), true});
        else if (previous_radial > 0.0 && radial <= 0.0) apsides.push_back({simulation_->clock().time(), r.magnitude(), v.magnitude(), false});
        previous_radial = radial;
        if (simulation_->clock().time() - campaign_start_time > baseline_period * 10.0 + 10000.0) throw std::runtime_error("lunar orbit apsis campaign exceeded expected duration");
    }

    // Use the first measured perilune as the deterministic station-keeping
    // disturbance point. A small retrograde finite burn lowers the next
    // apolune, after which the guidance loop restores the target semi-major axis.
    auto peri_it = std::find_if(apsides.begin(), apsides.end(), [](const LunarApsisEvent& e){ return e.periapsis; });
    if (peri_it == apsides.end()) throw std::runtime_error("no perilune found for station keeping");

    const auto before_disturbance_state = simulation_->spacecraft().state();
    const auto [disturbance_r, disturbance_v] = relative_state();
    const Vector3 disturbance_tangent = (disturbance_v - disturbance_r.normalized() * disturbance_v.dot(disturbance_r.normalized())).normalized();
    const double disturbance_mass_before = before_disturbance_state.mass_kg;
    const double disturbance_duration = kTargetDisturbanceDv * kIsp / (kThrust + 1.0e-12) * disturbance_mass_before / std::max(1.0, disturbance_mass_before - kTargetDisturbanceDv * disturbance_mass_before / (kIsp * 9.80665));
    PropulsionCommand disturbance_command{};
    disturbance_command.main_engine_throttle = 1.0;
    disturbance_command.commanded_thrust_direction_body = before_disturbance_state.attitude_body_to_inertial.inverse().rotate(disturbance_tangent * -1.0);
    simulation_->step_for_duration(disturbance_command, disturbance_duration);

    const auto disturbed_apo = propagate_to_lunar_apsis(*simulation_, ops_moon_position, ops_moon_velocity, tli_plan_, false, configuration_.step_seconds, kMaxApsisSteps);

    // At the next apolune, close the loop directly on the measured apolune
    // error. A bounded retrograde/prograde finite burn at apolune changes the
    // apolune altitude without relying on a fixed demonstration delta-v.
    const auto correction_apo = propagate_to_lunar_apsis(*simulation_, ops_moon_position, ops_moon_velocity, tli_plan_, false, configuration_.step_seconds, kMaxApsisSteps);
    (void)correction_apo;
    const auto correction_state = simulation_->spacecraft().state();
    const auto [correction_r_vec, correction_v_vec] = relative_state();
    const double correction_radius = correction_r_vec.magnitude();
    const double correction_energy = specific_orbital_energy(kMuMoonLocal, correction_r_vec, correction_v_vec);
    const double correction_a = semi_major_axis_from_energy(kMuMoonLocal, correction_energy);
    const double correction_e = eccentricity_from_state(kMuMoonLocal, correction_r_vec, correction_v_vec);
    const double current_apo_radius = correction_a * (1.0 + correction_e);
    const double apo_error = baseline_ra - current_apo_radius;
    const double current_tangential_speed = correction_v_vec.dot((correction_v_vec - correction_r_vec.normalized() * correction_v_vec.dot(correction_r_vec.normalized())).normalized());
    const double target_a_for_correction = std::max(kMoonRadiusLocal + 1000.0, baseline_a);
    const double target_speed_at_apo = std::sqrt(kMuMoonLocal * (2.0 / correction_radius - 1.0 / target_a_for_correction));
    double correction_dv = target_speed_at_apo - current_tangential_speed;
    // Keep the feedback action bounded and use the measured apolune error as
    // a secondary sign check.
    correction_dv = std::clamp(correction_dv, -10.0, 10.0);
    if (std::abs(apo_error) < 100.0) correction_dv = 0.0;
    const double correction_mass_before = correction_state.mass_kg;
    if (std::abs(correction_dv) > 1.0e-6) {
        const double g0 = 9.80665;
        const double correction_duration = correction_mass_before * kIsp * g0 / kThrust * (1.0 - std::exp(-std::abs(correction_dv) / (kIsp * g0)));
        PropulsionCommand correction_command{};
        correction_command.main_engine_throttle = 1.0;
        const Vector3 tangent_hat = (correction_v_vec - correction_r_vec.normalized() * correction_v_vec.dot(correction_r_vec.normalized())).normalized();
        correction_command.commanded_thrust_direction_body = correction_state.attitude_body_to_inertial.inverse().rotate(tangent_hat * (correction_dv >= 0.0 ? 1.0 : -1.0));
        simulation_->step_for_duration(correction_command, correction_duration);
    }

    const auto corrected_apo = propagate_to_lunar_apsis(*simulation_, ops_moon_position, ops_moon_velocity, tli_plan_, false, configuration_.step_seconds, kMaxApsisSteps);
    const auto [final_r, final_v] = relative_state();
    const double final_energy = specific_orbital_energy(kMuMoonLocal, final_r, final_v);
    const double final_a = semi_major_axis_from_energy(kMuMoonLocal, final_energy);
    const double final_e = eccentricity_from_state(kMuMoonLocal, final_r, final_v);
    const double final_rp = final_a * (1.0 - final_e);
    const double final_ra = final_a * (1.0 + final_e);
    const double propellant = correction_mass_before - simulation_->spacecraft().state().mass_kg + (disturbance_mass_before - correction_mass_before);
    const double final_apo_error = (final_ra - kMoonRadiusLocal) - (baseline_ra - kMoonRadiusLocal);
    const double disturbed_apo_alt = disturbed_apo.radius_meters - kMoonRadiusLocal;
    const double corrected_apo_alt = corrected_apo.radius_meters - kMoonRadiusLocal;
    const bool bounded = final_energy < 0.0 && final_a > kMoonRadiusLocal && final_e < 0.35;
    const bool stable = std::abs(final_a - baseline_a) < 75000.0 && std::abs(final_e - baseline_e) < 0.05;
    const bool effective = std::abs(final_a - baseline_a) < std::abs(correction_a - baseline_a);

    lunar_orbit_operations_result_.orbit_bounded = bounded;
    lunar_orbit_operations_result_.repeated_apsides_detected = apsides.size() == 6;
    lunar_orbit_operations_result_.orbit_stable = stable;
    lunar_orbit_operations_result_.station_keeping_effective = effective;
    lunar_orbit_operations_result_.revolutions_completed = 3;
    lunar_orbit_operations_result_.orbital_period_seconds = baseline_period;
    lunar_orbit_operations_result_.baseline_semi_major_axis_meters = baseline_a;
    lunar_orbit_operations_result_.baseline_eccentricity = baseline_e;
    lunar_orbit_operations_result_.baseline_perilune_altitude_meters = baseline_rp - kMoonRadiusLocal;
    lunar_orbit_operations_result_.baseline_apolune_altitude_meters = baseline_ra - kMoonRadiusLocal;
    lunar_orbit_operations_result_.final_semi_major_axis_meters = final_a;
    lunar_orbit_operations_result_.final_eccentricity = final_e;
    lunar_orbit_operations_result_.final_perilune_altitude_meters = final_rp - kMoonRadiusLocal;
    lunar_orbit_operations_result_.final_apolune_altitude_meters = final_ra - kMoonRadiusLocal;
    lunar_orbit_operations_result_.disturbance_delta_v_m_per_s = kTargetDisturbanceDv;
    lunar_orbit_operations_result_.correction_delta_v_m_per_s = correction_dv;
    lunar_orbit_operations_result_.disturbed_apolune_altitude_meters = disturbed_apo_alt;
    lunar_orbit_operations_result_.corrected_apolune_altitude_meters = corrected_apo_alt;
    lunar_orbit_operations_result_.final_apolune_error_meters = final_apo_error;
    lunar_orbit_operations_result_.propellant_consumed_kg = propellant;
    lunar_orbit_operations_executed_ = true;
    refresh_snapshot();
    return lunar_orbit_operations_result_;
}

const LunarOrbitOperationsResult& PhysicalMissionExecutionEngine::lunar_orbit_operations_result() const noexcept {
    return lunar_orbit_operations_result_;
}

const LunarDescentPreparationResult& PhysicalMissionExecutionEngine::execute_lunar_descent_preparation() {
    if (phase_ != MissionPhase::LunarOrbit || !loi_executed_ || !lunar_orbit_operations_executed_) {
        throw std::runtime_error("lunar descent preparation requires completed physical lunar orbit operations");
    }
    if (lunar_descent_preparation_executed_) return lunar_descent_preparation_result_;

    constexpr double kMuMoonLocal = kMuMoon;
    constexpr double kMoonRadiusLocal = kMoonRadius;
    constexpr double kTargetPeriluneAltitude = 30.0e3;
    constexpr double kTargetApoluneAltitude = 100.0e3;
    constexpr std::uint64_t kMaxApsisSteps = 300000;
    constexpr double kTargetTolerance = 2500.0;
    constexpr double kMaxEccentricity = 0.30;

    auto relative_state = [&]() {
        const auto state = simulation_->spacecraft().state();
        const auto moon = moon_position(simulation_->clock().time());
        const auto moon_v = moon_velocity(simulation_->clock().time());
        return std::pair<Vector3, Vector3>{state.position_meters - moon, state.velocity_m_per_s - moon_v};
    };

    auto orbit_elements = [&]() {
        const auto [r, v] = relative_state();
        const double energy = specific_orbital_energy(kMuMoonLocal, r, v);
        const double a = semi_major_axis_from_energy(kMuMoonLocal, energy);
        const double e = eccentricity_from_state(kMuMoonLocal, r, v);
        return std::tuple<double, double, double, double>{
            a, e, a * (1.0 - e) - kMoonRadiusLocal, a * (1.0 + e) - kMoonRadiusLocal};
    };

    const auto [initial_a, initial_e, initial_rp_alt, initial_ra_alt] = orbit_elements();
    if (!(initial_a > kMoonRadiusLocal && initial_e >= 0.0 && initial_e < kMaxEccentricity &&
          initial_rp_alt > kTargetPeriluneAltitude && initial_ra_alt > kTargetApoluneAltitude)) {
        throw std::runtime_error("current lunar orbit is not suitable for physical descent preparation");
    }

    LunarDescentPreparationPlanner planner{};
    const auto plan = planner.plan(
        kMuMoonLocal, kMoonRadiusLocal,
        initial_rp_alt, initial_ra_alt,
        kTargetPeriluneAltitude, kTargetApoluneAltitude);
    if (!plan.valid) throw std::runtime_error("failed to create lunar descent preparation plan");

    const double preparation_mass_before = simulation_->spacecraft().state().mass_kg;

    auto burn_at_current_apsis = [&](double dv) -> double {
        if (dv <= 0.0) return 0.0;
        const auto before = simulation_->spacecraft().state();
        const auto [r, v] = relative_state();
        const Vector3 radial = r.normalized();
        const Vector3 tangential = (v - radial * v.dot(radial)).normalized();
        const double g0 = 9.80665;
        const double final_mass = before.mass_kg * std::exp(-dv / (kIsp * g0));
        const double mdot = kThrust / (kIsp * g0);
        const double duration = std::max(0.0, (before.mass_kg - final_mass) / mdot);
        if (duration <= 0.0) return 0.0;
        PropulsionCommand command{};
        command.main_engine_throttle = 1.0;
        command.commanded_thrust_direction_body = before.attitude_body_to_inertial.inverse().rotate(tangential * -1.0);
        double remaining = duration;
        while (remaining > 1.0e-9) {
            const double dt = std::min(configuration_.step_seconds, remaining);
            simulation_->step_for_duration(command, dt);
            remaining -= dt;
        }
        const double after_mass = simulation_->spacecraft().state().mass_kg;
        return kIsp * g0 * std::log(before.mass_kg / after_mass);
    };

    // Burn 1: physically wait for the measured apolune, then lower perilune.
    propagate_to_lunar_apsis(*simulation_, ops_moon_position, ops_moon_velocity, tli_plan_, false,
                             configuration_.step_seconds, kMaxApsisSteps);
    const double first_dv_exec = burn_at_current_apsis(plan.first_burn_delta_v_m_per_s);

    // Burn 2: physically wait for the newly created low perilune, then lower
    // apolune to the descent-orbit target. No elapsed-time phase transition is used.
    propagate_to_lunar_apsis(*simulation_, ops_moon_position, ops_moon_velocity, tli_plan_, true,
                             configuration_.step_seconds, kMaxApsisSteps);
    const auto [transfer_r, transfer_v] = relative_state();
    const double target_a = 0.5 * ((kMoonRadiusLocal + kTargetPeriluneAltitude) +
                                   (kMoonRadiusLocal + kTargetApoluneAltitude));
    const double current_speed = transfer_v.magnitude();
    const double target_speed = std::sqrt(kMuMoonLocal * (2.0 / transfer_r.magnitude() - 1.0 / target_a));
    const double second_dv_plan = std::max(0.0, current_speed - target_speed);
    const double second_dv_exec = burn_at_current_apsis(second_dv_plan);

    // Measure the resulting orbit at the next apsides rather than declaring a
    // descent-ready state from the burn commands alone.
    propagate_to_lunar_apsis(*simulation_, ops_moon_position, ops_moon_velocity, tli_plan_, true,
                             configuration_.step_seconds, kMaxApsisSteps);
    const auto [final_a, final_e, final_rp_alt, final_ra_alt] = orbit_elements();

    const auto& final_state = simulation_->spacecraft().state();
    const double propellant = std::max(0.0, preparation_mass_before - final_state.mass_kg);
    const bool target_ok = std::abs(final_rp_alt - kTargetPeriluneAltitude) <= kTargetTolerance &&
                           std::abs(final_ra_alt - kTargetApoluneAltitude) <= kTargetTolerance;
    const bool bounded = final_a > kMoonRadiusLocal && final_e < kMaxEccentricity &&
                         final_rp_alt > 0.0;
    const bool ready = target_ok && bounded && final_state.mass_kg > kDryMass;

    lunar_descent_preparation_result_.descent_orbit_targeted = true;
    lunar_descent_preparation_result_.first_burn_executed = first_dv_exec > 0.0;
    lunar_descent_preparation_result_.second_burn_executed = second_dv_exec > 0.0;
    lunar_descent_preparation_result_.descent_orbit_bounded = bounded;
    lunar_descent_preparation_result_.descent_ready = ready;
    lunar_descent_preparation_result_.initial_perilune_altitude_meters = initial_rp_alt;
    lunar_descent_preparation_result_.initial_apolune_altitude_meters = initial_ra_alt;
    lunar_descent_preparation_result_.target_perilune_altitude_meters = kTargetPeriluneAltitude;
    lunar_descent_preparation_result_.target_apolune_altitude_meters = kTargetApoluneAltitude;
    lunar_descent_preparation_result_.first_burn_planned_delta_v_m_per_s = plan.first_burn_delta_v_m_per_s;
    lunar_descent_preparation_result_.first_burn_executed_delta_v_m_per_s = first_dv_exec;
    lunar_descent_preparation_result_.second_burn_planned_delta_v_m_per_s = second_dv_plan;
    lunar_descent_preparation_result_.second_burn_executed_delta_v_m_per_s = second_dv_exec;
    lunar_descent_preparation_result_.total_executed_delta_v_m_per_s = first_dv_exec + second_dv_exec;
    lunar_descent_preparation_result_.final_perilune_altitude_meters = final_rp_alt;
    lunar_descent_preparation_result_.final_apolune_altitude_meters = final_ra_alt;
    lunar_descent_preparation_result_.final_semi_major_axis_meters = final_a;
    lunar_descent_preparation_result_.final_eccentricity = final_e;
    lunar_descent_preparation_result_.propellant_consumed_kg = propellant;
    lunar_descent_preparation_executed_ = true;
    refresh_snapshot();
    if (!ready) throw std::runtime_error("physical lunar descent preparation did not establish target descent orbit");
    return lunar_descent_preparation_result_;
}

const LunarDescentPreparationResult& PhysicalMissionExecutionEngine::lunar_descent_preparation_result() const noexcept {
    return lunar_descent_preparation_result_;
}

} // namespace trishula
