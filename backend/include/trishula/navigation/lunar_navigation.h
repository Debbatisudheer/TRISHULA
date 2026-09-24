#pragma once

#include "trishula/core/vector3.h"

namespace trishula {

struct LunarNavigationState {
    double time_seconds{0.0};
    Vector3 moon_position_meters{};
    Vector3 spacecraft_position_meters{};
    Vector3 relative_position_meters{};
    Vector3 relative_velocity_m_per_s{};
    double moon_distance_meters{0.0};
    double altitude_above_moon_surface_meters{0.0};
    double relative_speed_m_per_s{0.0};
    double radial_velocity_m_per_s{0.0};
    double flight_path_angle_rad{0.0};
    double specific_orbital_energy_j_per_kg{0.0};
    double characteristic_energy_c3_m2_per_s2{0.0};
    double hyperbolic_excess_speed_m_per_s{0.0};
    double lunar_perilune_speed_m_per_s{0.0};
    bool hyperbolic_approach{false};
};

class LunarNavigator {
public:
    [[nodiscard]] LunarNavigationState determine(
        double time_seconds,
        const Vector3& spacecraft_position_meters,
        const Vector3& spacecraft_velocity_m_per_s,
        double moon_gravitational_parameter_m3_per_s2,
        double moon_radius_meters,
        double moon_orbit_radius_meters,
        double moon_orbit_period_seconds,
        double moon_phase_rad) const;
};

} // namespace trishula
