#include "trishula/surface/post_landing_operations.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {

PostLandingOperations::PostLandingOperations(SurfaceOperationParameters parameters)
    : parameters_(parameters) {
    if (parameters_.max_safe_tilt_rad <= 0.0 ||
        parameters_.max_safe_angular_rate_rad_s <= 0.0 ||
        parameters_.max_safe_vertical_speed_m_s <= 0.0 ||
        parameters_.minimum_battery_soc < 0.0 || parameters_.minimum_battery_soc > 1.0 ||
        parameters_.minimum_thermal_margin < 0.0 || parameters_.minimum_thermal_margin > 1.0 ||
        parameters_.stabilization_decay_per_s <= 0.0) {
        throw std::invalid_argument("Invalid post-landing operation parameters");
    }
}

SurfaceOperationMetrics PostLandingOperations::transition_to_surface_mode(SurfaceLanderState& state) const {
    const auto metrics = evaluate(state);
    if (!metrics.touchdown_state_accepted || metrics.fault_detected) {
        state.mode = LanderMode::Fault;
        SurfaceOperationMetrics failed = metrics;
        failed.surface_mode_entered = false;
        failed.fault_detected = true;
        return failed;
    }

    state.mode = LanderMode::SurfaceSafe;
    SurfaceOperationMetrics result = metrics;
    result.surface_mode_entered = true;
    return result;
}

SurfaceOperationMetrics PostLandingOperations::stabilize(
    SurfaceLanderState& state,
    double duration_s,
    double dt_s) const {
    if (duration_s < 0.0 || dt_s <= 0.0) {
        throw std::invalid_argument("Invalid stabilization interval");
    }

    const std::size_t steps = static_cast<std::size_t>(std::ceil(duration_s / dt_s));
    for (std::size_t i = 0; i < steps; ++i) {
        const double step = std::min(dt_s, std::max(0.0, duration_s - static_cast<double>(i) * dt_s));
        if (step <= 0.0) {
            break;
        }
        const double factor = std::exp(-parameters_.stabilization_decay_per_s * step);
        state.angular_velocity_rad_s *= factor;
        state.velocity_m_s.x *= std::exp(-0.7 * parameters_.stabilization_decay_per_s * step);
        state.velocity_m_s.y *= std::exp(-0.7 * parameters_.stabilization_decay_per_s * step);
        state.velocity_m_s.z *= std::exp(-parameters_.stabilization_decay_per_s * step);
    }

    state.tilt_rad *= std::exp(-parameters_.stabilization_decay_per_s * duration_s);
    return transition_to_surface_mode(state);
}

SurfaceOperationMetrics PostLandingOperations::deploy_surface_hardware(SurfaceLanderState& state) const {
    SurfaceOperationMetrics result = evaluate(state);
    if (state.mode == LanderMode::Fault || result.fault_detected) {
        state.mode = LanderMode::Fault;
        result.fault_detected = true;
        return result;
    }

    state.antenna = DeploymentStatus::Deployed;
    state.camera_mast = DeploymentStatus::Deployed;
    state.solar_array = DeploymentStatus::Deployed;

    result.antenna_deployed = true;
    result.camera_mast_deployed = true;
    result.solar_array_deployed = true;
    state.mode = LanderMode::SurfaceOperations;
    return result;
}

SurfaceOperationMetrics PostLandingOperations::prepare_rover_interface(SurfaceLanderState& state) const {
    SurfaceOperationMetrics result = evaluate(state);
    if (state.mode != LanderMode::SurfaceOperations ||
        state.antenna != DeploymentStatus::Deployed ||
        state.camera_mast != DeploymentStatus::Deployed ||
        state.solar_array != DeploymentStatus::Deployed ||
        result.fault_detected) {
        result.rover_interface_ready = false;
        result.fault_detected = true;
        state.mode = LanderMode::Fault;
        return result;
    }

    state.mode = LanderMode::RoverInterfaceReady;
    result.rover_interface_ready = true;
    return result;
}

SurfaceOperationMetrics PostLandingOperations::evaluate(SurfaceLanderState& state) const {
    SurfaceOperationMetrics result{};
    result.touchdown_state_accepted = state.landed_contact;
    result.attitude_stable = std::abs(state.tilt_rad) <= parameters_.max_safe_tilt_rad &&
        state.angular_velocity_rad_s.magnitude() <= parameters_.max_safe_angular_rate_rad_s &&
        std::abs(state.velocity_m_s.z) <= parameters_.max_safe_vertical_speed_m_s;
    result.landing_hardware_healthy = state.landing_legs_locked;
    result.power_system_healthy = state.battery_soc >= parameters_.minimum_battery_soc;
    result.thermal_system_healthy = state.thermal_margin >= parameters_.minimum_thermal_margin;
    result.fault_detected = !result.touchdown_state_accepted || !result.attitude_stable ||
        !result.landing_hardware_healthy || !result.power_system_healthy ||
        !result.thermal_system_healthy || !state.avionics_healthy || !state.propulsion_safe;
    result.antenna_deployed = state.antenna == DeploymentStatus::Deployed;
    result.camera_mast_deployed = state.camera_mast == DeploymentStatus::Deployed;
    result.solar_array_deployed = state.solar_array == DeploymentStatus::Deployed;
    result.rover_interface_ready = state.mode == LanderMode::RoverInterfaceReady;
    result.surface_mode_entered = state.mode == LanderMode::SurfaceSafe ||
        state.mode == LanderMode::SurfaceOperations || state.mode == LanderMode::RoverInterfaceReady;
    return result;
}

const char* PostLandingOperations::mode_name(LanderMode mode) noexcept {
    switch (mode) {
    case LanderMode::LandedTransition: return "LANDED_TRANSITION";
    case LanderMode::SurfaceSafe: return "SURFACE_SAFE";
    case LanderMode::SurfaceOperations: return "SURFACE_OPERATIONS";
    case LanderMode::RoverInterfaceReady: return "ROVER_INTERFACE_READY";
    case LanderMode::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

const char* PostLandingOperations::deployment_name(DeploymentStatus status) noexcept {
    switch (status) {
    case DeploymentStatus::Stowed: return "STOWED";
    case DeploymentStatus::Deployed: return "DEPLOYED";
    case DeploymentStatus::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

} // namespace trishula
