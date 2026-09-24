#pragma once

#include "trishula/environment/celestial_body.h"
#include "trishula/estimation/state_estimator.h"

namespace trishula {

struct OrbitalElements {
    double specific_angular_momentum_m2_per_s{0.0};
    double eccentricity{0.0};
    double semi_major_axis_meters{0.0};
    double inclination_rad{0.0};
    double right_ascension_of_ascending_node_rad{0.0};
    double argument_of_periapsis_rad{0.0};
    double true_anomaly_rad{0.0};
    double specific_orbital_energy_j_per_kg{0.0};
    bool hyperbolic_or_parabolic{false};
};

struct NavigationState {
    double time_seconds{0.0};
    Vector3 position_meters{};
    Vector3 velocity_m_per_s{};
    double radial_distance_meters{0.0};
    double altitude_meters{0.0};
    double speed_m_per_s{0.0};
    double radial_velocity_m_per_s{0.0};
    double flight_path_angle_rad{0.0};
    double orbital_period_seconds{0.0};
    OrbitalElements orbit{};
};

class EarthCenteredNavigator {
public:
    [[nodiscard]] NavigationState determine(
        const EstimatedState& estimated_state,
        const CelestialBody& primary_body) const;
};

} // namespace trishula
