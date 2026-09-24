#pragma once

#include "trishula/core/simulation_engine.h"
#include "trishula/landing/lander_contact_dynamics.h"
#include "trishula/landing/lunar_terrain.h"

#include <cstdint>
#include <functional>

namespace trishula {

struct PhysicalLunarLandingIntegrationConfiguration {
    double contact_start_altitude_meters{50.0};
    double control_step_seconds{0.005};
    double maximum_contact_duration_seconds{30.0};
    double maximum_touchdown_speed_m_per_s{20.0};
    double maximum_landed_tilt_rad{0.13962634015954636}; // 8 degrees
    double maximum_landed_angular_rate_rad_s{0.05};
    std::uint32_t minimum_contact_legs{3};
    LanderContactParameters contact_parameters{};
    LanderContactGeometry contact_geometry{};
};

struct PhysicalLunarLandingIntegrationResult {
    bool valid_initial_state{false};
    bool terminal_descent_boundary_reached{false};
    bool touchdown_detected{false};
    bool stable_contact{false};
    bool landed_state{false};
    bool tip_over_detected{false};

    double initial_altitude_meters{0.0};
    double final_altitude_meters{0.0};
    double initial_vertical_velocity_m_per_s{0.0};
    double final_vertical_velocity_m_per_s{0.0};
    double touchdown_speed_m_per_s{0.0};
    double final_tilt_rad{0.0};
    double final_angular_rate_rad_s{0.0};
    double maximum_contact_force_newtons{0.0};
    double maximum_shock_acceleration_m_per_s2{0.0};
    double duration_seconds{0.0};
    std::uint64_t physical_contact_steps{0};

    Vector3 final_inertial_position_meters{};
    Vector3 final_inertial_velocity_m_per_s{};
};

class PhysicalLunarLandingIntegrationExecutor {
public:
    using EphemerisFunction = std::function<Vector3(double)>;

    PhysicalLunarLandingIntegrationExecutor(
        PhysicalLunarLandingIntegrationConfiguration configuration,
        EphemerisFunction moon_position,
        EphemerisFunction moon_velocity,
        double moon_radius_meters);

    [[nodiscard]] PhysicalLunarLandingIntegrationResult execute(
        SimulationEngine& simulation) const;

private:
    [[nodiscard]] Vector3 relative_position(const SimulationEngine& simulation) const;
    [[nodiscard]] Vector3 relative_velocity(const SimulationEngine& simulation) const;

    PhysicalLunarLandingIntegrationConfiguration configuration_{};
    EphemerisFunction moon_position_{};
    EphemerisFunction moon_velocity_{};
    double moon_radius_meters_{0.0};
};

} // namespace trishula
