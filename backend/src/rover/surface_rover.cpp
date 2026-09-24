#include "trishula/rover/surface_rover.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace trishula {

SurfaceRover::SurfaceRover(RoverParameters parameters) : parameters_(parameters) {
    if (parameters_.wheelbase_m <= 0.0 || parameters_.track_m <= 0.0 ||
        parameters_.wheel_radius_m <= 0.0 || parameters_.max_speed_m_s <= 0.0 ||
        parameters_.max_acceleration_m_s2 <= 0.0 || parameters_.max_slope <= 0.0 ||
        parameters_.max_roughness_m <= 0.0 || parameters_.localization_noise_m < 0.0 ||
        parameters_.obstacle_lookahead_m <= 0.0 || parameters_.wheel_slip_limit <= 0.0) {
        throw std::invalid_argument("Invalid rover parameters");
    }
}

bool SurfaceRover::deploy_from_lander(RoverState& state) const {
    if (state.mode != RoverMode::Stowed || !state.drive_system_healthy || state.battery_soc < 0.35) {
        state.mode = RoverMode::Fault;
        return false;
    }
    state.mode = RoverMode::Deployment;
    state.deployed = true;
    state.wheels_on_surface = true;
    state.speed_m_s = 0.0;
    return true;
}

bool SurfaceRover::initialize_surface_systems(RoverState& state) const {
    if (state.mode != RoverMode::Deployment || !state.deployed || !state.wheels_on_surface ||
        !state.drive_system_healthy || state.battery_soc < 0.30) {
        state.mode = RoverMode::Fault;
        return false;
    }
    state.mast_ready = true;
    state.localization_valid = true;
    state.mode = RoverMode::SurfaceReady;
    return true;
}

RoverStepMetrics SurfaceRover::drive_to_target(RoverState& state,
                                              const LunarTerrainMap& terrain,
                                              const RoverMissionTarget& target,
                                              std::size_t steps,
                                              double dt_s) const {
    if (dt_s <= 0.0) {
        throw std::invalid_argument("Rover integration step must be positive");
    }
    if (state.mode != RoverMode::SurfaceReady && state.mode != RoverMode::Driving &&
        state.mode != RoverMode::HazardHold) {
        throw std::invalid_argument("Rover must be surface-ready before driving");
    }

    RoverStepMetrics last = evaluate(state, terrain, target);
    for (std::size_t i = 0; i < steps; ++i) {
        last = evaluate(state, terrain, target);
        if (last.target_reached) {
            state.speed_m_s = 0.0;
            state.mode = RoverMode::MissionComplete;
            return last;
        }
        if (last.hazard_detected) {
            state.speed_m_s = 0.0;
            state.mode = RoverMode::HazardHold;
            return last;
        }

        last = step_toward_waypoint(state, terrain, target, dt_s);
        if (state.mode == RoverMode::HazardHold || state.mode == RoverMode::Fault) {
            return last;
        }
    }

    last = evaluate(state, terrain, target);
    if (last.target_reached) {
        state.speed_m_s = 0.0;
        state.mode = RoverMode::MissionComplete;
    }
    return last;
}

RoverStepMetrics SurfaceRover::step_toward_waypoint(RoverState& state,
                                                    const LunarTerrainMap& terrain,
                                                    const RoverMissionTarget& target,
                                                    double dt_s) const {
    if (dt_s <= 0.0) {
        throw std::invalid_argument("Rover integration step must be positive");
    }
    if (state.mode != RoverMode::SurfaceReady && state.mode != RoverMode::Driving) {
        throw std::invalid_argument("Rover must be surface-ready before waypoint stepping");
    }

    RoverStepMetrics current = evaluate(state, terrain, target);
    if (current.hazard_detected) {
        state.speed_m_s = 0.0;
        state.mode = RoverMode::HazardHold;
        return current;
    }

    state.mode = RoverMode::Driving;
    const double dx = target.x_m - state.x_m;
    const double dy = target.y_m - state.y_m;
    const double desired_heading = std::atan2(dy, dx);
    double heading_error = desired_heading - state.heading_rad;
    constexpr double pi = std::numbers::pi;
    while (heading_error > pi) heading_error -= 2.0 * pi;
    while (heading_error < -pi) heading_error += 2.0 * pi;
    state.heading_rad += std::clamp(heading_error, -0.2, 0.2);

    const double distance = std::hypot(dx, dy);
    const double braking_speed = std::sqrt(2.0 * parameters_.max_acceleration_m_s2 * distance);
    const double desired_speed = std::min(parameters_.max_speed_m_s, braking_speed);
    const double speed_delta = desired_speed - state.speed_m_s;
    state.speed_m_s += std::clamp(speed_delta,
                                  -parameters_.max_acceleration_m_s2 * dt_s,
                                  parameters_.max_acceleration_m_s2 * dt_s);

    const double distance_step = state.speed_m_s * dt_s;
    state.x_m += distance_step * std::cos(state.heading_rad);
    state.y_m += distance_step * std::sin(state.heading_rad);
    state.battery_soc = std::max(0.0, state.battery_soc - 0.00008 * dt_s - 0.00002 * std::abs(state.speed_m_s));

    if (state.battery_soc < 0.15) {
        state.speed_m_s = 0.0;
        state.mode = RoverMode::Fault;
        current = evaluate(state, terrain, target);
        current.hazard_detected = true;
        return current;
    }

    return evaluate(state, terrain, target);
}

RoverStepMetrics SurfaceRover::evaluate(const RoverState& state,
                                       const LunarTerrainMap& terrain,
                                       const RoverMissionTarget& target) const {
    RoverStepMetrics result{};
    result.distance_to_target_m = std::hypot(target.x_m - state.x_m, target.y_m - state.y_m);
    result.estimated_x_m = state.x_m + parameters_.localization_noise_m;
    result.estimated_y_m = state.y_m - 0.5 * parameters_.localization_noise_m;
    result.terrain_slope = terrain_slope_along_path(terrain, state.x_m, state.y_m);
    result.terrain_roughness_m = terrain_roughness_along_path(terrain, state.x_m, state.y_m);
    result.hazard_detected = hazard_ahead(terrain, state.x_m, state.y_m, state.heading_rad);
    result.target_reached = result.distance_to_target_m <= target.acceptance_radius_m &&
        std::abs(state.speed_m_s) <= 0.10;
    result.localization_valid = state.localization_valid && parameters_.localization_noise_m < 1.0;
    result.wheel_slip_detected = state.speed_m_s > parameters_.max_speed_m_s + 0.1;
    return result;
}

double SurfaceRover::terrain_slope_along_path(const LunarTerrainMap& terrain,
                                              double x_m,
                                              double /*y_m*/) const {
    return terrain.slope(x_m);
}

double SurfaceRover::terrain_roughness_along_path(const LunarTerrainMap& terrain,
                                                  double x_m,
                                                  double /*y_m*/) const {
    return terrain.roughness(x_m);
}

bool SurfaceRover::hazard_ahead(const LunarTerrainMap& terrain,
                                double x_m,
                                double /*y_m*/,
                                double heading_rad) const {
    for (int i = 1; i <= 5; ++i) {
        const double look = parameters_.obstacle_lookahead_m * static_cast<double>(i) / 5.0;
        const double x = x_m + look * std::cos(heading_rad);
        const auto sample = terrain.sample(x);
        if (sample.crater || sample.boulder_field ||
            sample.slope > parameters_.max_slope || sample.roughness_m > parameters_.max_roughness_m) {
            return true;
        }
    }
    return false;
}

const char* SurfaceRover::mode_name(RoverMode mode) noexcept {
    switch (mode) {
    case RoverMode::Stowed: return "STOWED";
    case RoverMode::Deployment: return "DEPLOYMENT";
    case RoverMode::SurfaceReady: return "SURFACE_READY";
    case RoverMode::Driving: return "DRIVING";
    case RoverMode::HazardHold: return "HAZARD_HOLD";
    case RoverMode::MissionComplete: return "MISSION_COMPLETE";
    case RoverMode::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

} // namespace trishula
