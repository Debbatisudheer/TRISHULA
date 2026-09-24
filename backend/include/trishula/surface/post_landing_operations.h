#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "trishula/core/vector3.h"

namespace trishula {

enum class LanderMode {
    LandedTransition,
    SurfaceSafe,
    SurfaceOperations,
    RoverInterfaceReady,
    Fault
};

enum class DeploymentStatus {
    Stowed,
    Deployed,
    Fault
};

struct SurfaceLanderState {
    LanderMode mode{LanderMode::LandedTransition};
    Vector3 position_m{};
    Vector3 velocity_m_s{};
    Vector3 angular_velocity_rad_s{};
    double tilt_rad{0.0};
    double mass_kg{0.0};
    double battery_soc{1.0};
    double thermal_margin{1.0};
    bool landed_contact{false};
    bool landing_legs_locked{true};
    DeploymentStatus antenna{DeploymentStatus::Stowed};
    DeploymentStatus camera_mast{DeploymentStatus::Stowed};
    DeploymentStatus solar_array{DeploymentStatus::Stowed};
    bool avionics_healthy{true};
    bool propulsion_safe{true};
};

struct SurfaceOperationParameters {
    double max_safe_tilt_rad{0.14};
    double max_safe_angular_rate_rad_s{0.05};
    double max_safe_vertical_speed_m_s{0.35};
    double minimum_battery_soc{0.25};
    double minimum_thermal_margin{0.20};
    double stabilization_decay_per_s{1.5};
};

struct SurfaceOperationMetrics {
    bool touchdown_state_accepted{false};
    bool attitude_stable{false};
    bool landing_hardware_healthy{false};
    bool power_system_healthy{false};
    bool thermal_system_healthy{false};
    bool surface_mode_entered{false};
    bool antenna_deployed{false};
    bool camera_mast_deployed{false};
    bool solar_array_deployed{false};
    bool rover_interface_ready{false};
    bool fault_detected{false};
};

class PostLandingOperations {
public:
    explicit PostLandingOperations(SurfaceOperationParameters parameters = {});

    [[nodiscard]] SurfaceOperationMetrics transition_to_surface_mode(SurfaceLanderState& state) const;
    [[nodiscard]] SurfaceOperationMetrics stabilize(SurfaceLanderState& state, double duration_s, double dt_s = 0.1) const;
    [[nodiscard]] SurfaceOperationMetrics deploy_surface_hardware(SurfaceLanderState& state) const;
    [[nodiscard]] SurfaceOperationMetrics prepare_rover_interface(SurfaceLanderState& state) const;
    [[nodiscard]] SurfaceOperationMetrics evaluate(SurfaceLanderState& state) const;

    [[nodiscard]] static const char* mode_name(LanderMode mode) noexcept;
    [[nodiscard]] static const char* deployment_name(DeploymentStatus status) noexcept;

private:
    SurfaceOperationParameters parameters_{};
};

} // namespace trishula
