#include "trishula/mission/mission_execution.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kMuEarth = 3.986004418e14;
constexpr double kMuMoon = 4.9048695e12;
constexpr double kEarthRadius = 6.371e6;
constexpr double kMoonRadius = 1.7374e6;
constexpr double kMoonOrbitRadius = 384400.0e3;
constexpr double kParkingAltitude = 400.0e3;
constexpr double kLunarPeriluneAltitude = 100.0e3;
constexpr double kLunarApoluneAltitude = 1000.0e3;
constexpr double kIncomingVInf = 787.785675;
constexpr double kThrust = 5.0e6;
constexpr double kIsp = 450.0;
constexpr double kInitialMass = 50000.0;
constexpr double kDryMass = 20000.0;
constexpr double kLaunchVelocity = 7800.0;
constexpr double kLaunchAltitude = 400000.0;
constexpr double kSurfaceDescentAltitude = 30000.0;
constexpr double kSurfaceMass = 30000.0;

bool positive(double value) { return value > 0.0; }

double clamp01(double value) { return std::clamp(value, 0.0, 1.0); }
}

MissionExecutionEngine::MissionExecutionEngine(MissionExecutionConfiguration configuration)
    : configuration_(configuration) {
    if (!positive(configuration_.step_seconds) ||
        !positive(configuration_.launch_duration_seconds) ||
        !positive(configuration_.earth_orbit_duration_seconds) ||
        !positive(configuration_.tli_duration_seconds) ||
        !positive(configuration_.lunar_cruise_duration_seconds) ||
        !positive(configuration_.lunar_orbit_duration_seconds) ||
        !positive(configuration_.descent_duration_seconds) ||
        !positive(configuration_.landing_duration_seconds) ||
        !positive(configuration_.rover_deployment_duration_seconds)) {
        throw std::invalid_argument("Mission execution durations must be positive");
    }
    reset();
}

void MissionExecutionEngine::reset() {
    mission_time_seconds_ = 0.0;
    phase_elapsed_seconds_ = 0.0;
    telemetry_sequence_ = 0;
    phase_ = MissionPhase::Launch;
    terrain_ = LunarTerrainMap(0.0);
    rover_state_ = {};
    rover_target_ = {configuration_.surface_target_x_m, configuration_.surface_target_y_m, 1.5};
    lander_state_ = {};
    lander_state_.mass_kg = kSurfaceMass;
    lander_state_.battery_soc = 0.95;
    lander_state_.thermal_margin = 0.90;
    tli_plan_ = TransLunarInjectionPlanner{}.plan(
        kEarthRadius + kParkingAltitude,
        kMoonOrbitRadius,
        kMoonRadius + kLunarPeriluneAltitude,
        kMuEarth,
        kMuMoon);
    loi_plan_ = LunarOrbitInsertionPlanner{}.plan(
        kMuMoon,
        kMoonRadius,
        kLunarPeriluneAltitude,
        kLunarApoluneAltitude,
        kIncomingVInf,
        kThrust,
        kIsp,
        kInitialMass,
        kDryMass);
    refresh_snapshot();
}

void MissionExecutionEngine::step() {
    if (complete()) return;

    mission_time_seconds_ += configuration_.step_seconds;
    phase_elapsed_seconds_ += configuration_.step_seconds;
    ++telemetry_sequence_;

    switch (phase_) {
    case MissionPhase::Launch:
    case MissionPhase::EarthOrbit:
    case MissionPhase::TransLunarInjection:
    case MissionPhase::LunarCruise:
    case MissionPhase::LunarOrbit:
        update_flight_state();
        break;
    case MissionPhase::Descent:
        update_descent();
        break;
    case MissionPhase::Landing:
        update_landing();
        break;
    case MissionPhase::RoverDeployment:
    case MissionPhase::SurfaceOperations:
        update_surface();
        break;
    case MissionPhase::Complete:
    case MissionPhase::Fault:
        break;
    }

    update_phase();
    refresh_snapshot();
}

void MissionExecutionEngine::run_until_complete(std::uint64_t max_steps) {
    std::uint64_t steps = 0;
    while (!complete() && steps < max_steps) {
        step();
        ++steps;
    }
    if (!complete()) throw std::runtime_error("Mission execution exceeded maximum step count");
}

void MissionExecutionEngine::enter_phase(MissionPhase phase) {
    phase_ = phase;
    phase_elapsed_seconds_ = 0.0;
    if (phase_ == MissionPhase::Landing) {
        lander_state_.landed_contact = false;
        lander_state_.mode = LanderMode::LandedTransition;
        lander_state_.velocity_m_s = {0.0, 0.0, -0.4};
        lander_state_.tilt_rad = 0.03;
    }
    if (phase_ == MissionPhase::RoverDeployment) {
        lander_state_.landed_contact = true;
        lander_state_.velocity_m_s = {};
        lander_state_.angular_velocity_rad_s = {};
        lander_state_.tilt_rad = 0.0;
    }
}

void MissionExecutionEngine::update_phase() {
    double duration = 1.0;
    switch (phase_) {
    case MissionPhase::Launch: duration = configuration_.launch_duration_seconds; break;
    case MissionPhase::EarthOrbit: duration = configuration_.earth_orbit_duration_seconds; break;
    case MissionPhase::TransLunarInjection: duration = configuration_.tli_duration_seconds; break;
    case MissionPhase::LunarCruise: duration = configuration_.lunar_cruise_duration_seconds; break;
    case MissionPhase::LunarOrbit: duration = configuration_.lunar_orbit_duration_seconds; break;
    case MissionPhase::Descent: duration = configuration_.descent_duration_seconds; break;
    case MissionPhase::Landing: duration = configuration_.landing_duration_seconds; break;
    case MissionPhase::RoverDeployment: duration = configuration_.rover_deployment_duration_seconds; break;
    case MissionPhase::SurfaceOperations: duration = 1.0; break;
    case MissionPhase::Complete:
    case MissionPhase::Fault: return;
    }

    if (phase_elapsed_seconds_ + 1e-12 < duration) return;

    switch (phase_) {
    case MissionPhase::Launch: enter_phase(MissionPhase::EarthOrbit); break;
    case MissionPhase::EarthOrbit: enter_phase(MissionPhase::TransLunarInjection); break;
    case MissionPhase::TransLunarInjection: enter_phase(MissionPhase::LunarCruise); break;
    case MissionPhase::LunarCruise: enter_phase(MissionPhase::LunarOrbit); break;
    case MissionPhase::LunarOrbit: enter_phase(MissionPhase::Descent); break;
    case MissionPhase::Descent: enter_phase(MissionPhase::Landing); break;
    case MissionPhase::Landing: enter_phase(MissionPhase::RoverDeployment); break;
    case MissionPhase::RoverDeployment: enter_phase(MissionPhase::SurfaceOperations); break;
    case MissionPhase::SurfaceOperations:
        if (rover_state_.mode == RoverMode::MissionComplete) enter_phase(MissionPhase::Complete);
        break;
    case MissionPhase::Complete:
    case MissionPhase::Fault: break;
    }
}

void MissionExecutionEngine::update_flight_state() {
    const double p = clamp01(phase_elapsed_seconds_ / std::max(configuration_.step_seconds, 1.0));
    (void)p;
    const double total_flight = configuration_.launch_duration_seconds +
        configuration_.earth_orbit_duration_seconds + configuration_.tli_duration_seconds +
        configuration_.lunar_cruise_duration_seconds + configuration_.lunar_orbit_duration_seconds;
    const double normalized = clamp01(mission_time_seconds_ / total_flight);

    double radius = kEarthRadius + kParkingAltitude;
    if (phase_ == MissionPhase::Launch) {
        const double q = clamp01(phase_elapsed_seconds_ / configuration_.launch_duration_seconds);
        radius = kEarthRadius + kParkingAltitude * q;
    } else if (phase_ == MissionPhase::EarthOrbit) {
        radius = kEarthRadius + kParkingAltitude;
    } else if (phase_ == MissionPhase::TransLunarInjection) {
        const double q = clamp01(phase_elapsed_seconds_ / configuration_.tli_duration_seconds);
        radius = kEarthRadius + kParkingAltitude + q * (kMoonOrbitRadius - kEarthRadius - kParkingAltitude) * 0.03;
    } else {
        const double q = clamp01((mission_time_seconds_ - configuration_.launch_duration_seconds -
            configuration_.earth_orbit_duration_seconds - configuration_.tli_duration_seconds) /
            std::max(configuration_.lunar_cruise_duration_seconds, 1.0));
        radius = (phase_ == MissionPhase::LunarCruise || phase_ == MissionPhase::LunarOrbit)
            ? kEarthRadius + kParkingAltitude + q * (kMoonOrbitRadius - kEarthRadius - kParkingAltitude)
            : kEarthRadius + kParkingAltitude;
    }

    snapshot_.altitude_m = std::max(0.0, radius - kEarthRadius);
    snapshot_.velocity_m_per_s = phase_ == MissionPhase::EarthOrbit ? kLaunchVelocity :
        std::max(250.0, tli_plan_.parking_orbit_speed_m_per_s +
            (tli_plan_.tli_delta_v_m_per_s * (phase_ == MissionPhase::TransLunarInjection ? 1.0 : 0.0)));
    snapshot_.spacecraft_x_m = radius * std::cos(2.0 * 3.141592653589793 * normalized);
    snapshot_.spacecraft_y_m = radius * std::sin(2.0 * 3.141592653589793 * normalized);
    snapshot_.distance_to_moon_m = std::abs(kMoonOrbitRadius - radius);
    snapshot_.battery_soc = std::max(0.70, 1.0 - 0.15 * normalized);
    snapshot_.propellant_fraction = std::max(0.45, 1.0 - 0.30 * normalized);
}

void MissionExecutionEngine::update_descent() {
    const double q = clamp01(phase_elapsed_seconds_ / configuration_.descent_duration_seconds);
    const double altitude = kSurfaceDescentAltitude * (1.0 - q);
    snapshot_.altitude_m = altitude;
    snapshot_.velocity_m_per_s = -50.0 + 49.5 * q;
    snapshot_.distance_to_moon_m = kMoonRadius + altitude;
    snapshot_.spacecraft_x_m = kMoonRadius + altitude;
    snapshot_.spacecraft_y_m = 0.0;
    snapshot_.battery_soc = 0.85 - 0.05 * q;
    snapshot_.propellant_fraction = 0.70 - 0.15 * q;
}

void MissionExecutionEngine::update_landing() {
    const double q = clamp01(phase_elapsed_seconds_ / configuration_.landing_duration_seconds);
    snapshot_.altitude_m = 10.0 * (1.0 - q);
    snapshot_.velocity_m_per_s = -0.4 * (1.0 - q);
    snapshot_.distance_to_moon_m = kMoonRadius + snapshot_.altitude_m;
    snapshot_.spacecraft_x_m = kMoonRadius + snapshot_.altitude_m;
    snapshot_.spacecraft_y_m = 0.0;
    if (q >= 0.999) {
        lander_state_.landed_contact = true;
        lander_state_.velocity_m_s = {};
        snapshot_.landed = true;
    }
}

void MissionExecutionEngine::update_surface() {
    if (phase_ == MissionPhase::RoverDeployment) {
        if (!rover_state_.deployed && phase_elapsed_seconds_ + 1e-12 >= configuration_.rover_deployment_duration_seconds * 0.25) {
            const bool deployed = rover_.deploy_from_lander(rover_state_);
            if (!deployed) phase_ = MissionPhase::Fault;
        }
        if (rover_state_.mode == RoverMode::Deployment && phase_elapsed_seconds_ + 1e-12 >= configuration_.rover_deployment_duration_seconds * 0.75) {
            const bool initialized = rover_.initialize_surface_systems(rover_state_);
            if (!initialized) phase_ = MissionPhase::Fault;
        }
        snapshot_.rover_deployed = rover_state_.deployed;
        snapshot_.rover_surface_ready = rover_state_.mode == RoverMode::SurfaceReady;
        return;
    }

    if (rover_state_.mode == RoverMode::SurfaceReady || rover_state_.mode == RoverMode::Driving) {
        const auto metrics = rover_.drive_to_target(rover_state_, terrain_, rover_target_, 1, configuration_.step_seconds);
        if (rover_state_.mode == RoverMode::Fault || metrics.hazard_detected) phase_ = MissionPhase::Fault;
    }
    snapshot_.rover_x_m = rover_state_.x_m;
    snapshot_.rover_y_m = rover_state_.y_m;
    snapshot_.rover_speed_m_per_s = rover_state_.speed_m_s;
    snapshot_.rover_deployed = rover_state_.deployed;
    snapshot_.rover_surface_ready = rover_state_.mode == RoverMode::SurfaceReady ||
        rover_state_.mode == RoverMode::Driving || rover_state_.mode == RoverMode::MissionComplete;
    snapshot_.rover_mission_complete = rover_state_.mode == RoverMode::MissionComplete;
}

void MissionExecutionEngine::refresh_snapshot() {
    snapshot_.mission_id = "TRISHULA";
    snapshot_.phase = phase_;
    snapshot_.mission_time_seconds = mission_time_seconds_;
    snapshot_.phase_elapsed_seconds = phase_elapsed_seconds_;
    double duration = 1.0;
    switch (phase_) {
    case MissionPhase::Launch: duration = configuration_.launch_duration_seconds; break;
    case MissionPhase::EarthOrbit: duration = configuration_.earth_orbit_duration_seconds; break;
    case MissionPhase::TransLunarInjection: duration = configuration_.tli_duration_seconds; break;
    case MissionPhase::LunarCruise: duration = configuration_.lunar_cruise_duration_seconds; break;
    case MissionPhase::LunarOrbit: duration = configuration_.lunar_orbit_duration_seconds; break;
    case MissionPhase::Descent: duration = configuration_.descent_duration_seconds; break;
    case MissionPhase::Landing: duration = configuration_.landing_duration_seconds; break;
    case MissionPhase::RoverDeployment: duration = configuration_.rover_deployment_duration_seconds; break;
    case MissionPhase::SurfaceOperations: duration = 1.0; break;
    case MissionPhase::Complete:
    case MissionPhase::Fault: duration = 1.0; break;
    }
    snapshot_.phase_progress = clamp01(phase_elapsed_seconds_ / duration);
    snapshot_.telemetry_sequence = telemetry_sequence_;
    snapshot_.mode = phase_name(phase_);
    snapshot_.rover_x_m = rover_state_.x_m;
    snapshot_.rover_y_m = rover_state_.y_m;
    snapshot_.rover_speed_m_per_s = rover_state_.speed_m_s;
    snapshot_.rover_deployed = rover_state_.deployed;
    snapshot_.rover_surface_ready = rover_state_.mode == RoverMode::SurfaceReady ||
        rover_state_.mode == RoverMode::Driving || rover_state_.mode == RoverMode::MissionComplete;
    snapshot_.rover_mission_complete = rover_state_.mode == RoverMode::MissionComplete;
}

const MissionExecutionSnapshot& MissionExecutionEngine::snapshot() const noexcept { return snapshot_; }
MissionPhase MissionExecutionEngine::phase() const noexcept { return phase_; }
bool MissionExecutionEngine::complete() const noexcept { return phase_ == MissionPhase::Complete; }
const TransLunarInjectionPlan& MissionExecutionEngine::tli_plan() const noexcept { return tli_plan_; }
const LunarOrbitInsertionPlan& MissionExecutionEngine::loi_plan() const noexcept { return loi_plan_; }
const RoverState& MissionExecutionEngine::rover_state() const noexcept { return rover_state_; }
const SurfaceLanderState& MissionExecutionEngine::lander_state() const noexcept { return lander_state_; }

const char* MissionExecutionEngine::phase_name(MissionPhase phase) noexcept {
    switch (phase) {
    case MissionPhase::Launch: return "LAUNCH";
    case MissionPhase::EarthOrbit: return "EARTH_ORBIT";
    case MissionPhase::TransLunarInjection: return "TRANS_LUNAR_INJECTION";
    case MissionPhase::LunarCruise: return "LUNAR_CRUISE";
    case MissionPhase::LunarOrbit: return "LUNAR_ORBIT";
    case MissionPhase::Descent: return "DESCENT";
    case MissionPhase::Landing: return "LANDING";
    case MissionPhase::RoverDeployment: return "ROVER_DEPLOYMENT";
    case MissionPhase::SurfaceOperations: return "SURFACE_OPERATIONS";
    case MissionPhase::Complete: return "COMPLETE";
    case MissionPhase::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

} // namespace trishula
