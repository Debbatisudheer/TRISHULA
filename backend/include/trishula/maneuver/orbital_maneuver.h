#pragma once

#include <string>

namespace trishula {

struct OrbitalManeuverPlan {
    double initial_radius_meters{0.0};
    double target_radius_meters{0.0};
    double initial_circular_speed_m_per_s{0.0};
    double target_circular_speed_m_per_s{0.0};
    double transfer_semi_major_axis_meters{0.0};
    double transfer_periapsis_speed_m_per_s{0.0};
    double transfer_apoapsis_speed_m_per_s{0.0};
    double first_burn_delta_v_m_per_s{0.0};
    double second_burn_delta_v_m_per_s{0.0};
    double total_delta_v_m_per_s{0.0};
    double transfer_time_seconds{0.0};
    double first_burn_duration_seconds{0.0};
    double second_burn_duration_seconds{0.0};
    int first_burn_direction_sign{0};
    int second_burn_direction_sign{0};
    bool is_raise_maneuver{false};
};

class HohmannTransferPlanner {
public:
    [[nodiscard]] OrbitalManeuverPlan plan(
        double initial_radius_meters,
        double target_radius_meters,
        double gravitational_parameter_m3_per_s2,
        double initial_mass_kg,
        double dry_mass_kg,
        double engine_max_thrust_newtons,
        double engine_specific_impulse_seconds) const;

private:
    [[nodiscard]] static double finite_burn_duration(
        double delta_v_m_per_s,
        double initial_mass_kg,
        double dry_mass_kg,
        double maximum_thrust_newtons,
        double specific_impulse_seconds);
};

} // namespace trishula
