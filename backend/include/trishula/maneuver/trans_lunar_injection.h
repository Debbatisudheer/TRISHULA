#pragma once

namespace trishula {

struct TransLunarInjectionPlan {
    double parking_orbit_radius_meters{0.0};
    double lunar_orbit_radius_meters{0.0};
    double transfer_semi_major_axis_meters{0.0};
    double parking_orbit_speed_m_per_s{0.0};
    double tli_perigee_speed_m_per_s{0.0};
    double tli_delta_v_m_per_s{0.0};
    double hyperbolic_excess_speed_m_per_s{0.0};
    double characteristic_energy_c3_m2_per_s2{0.0};
    double transfer_time_seconds{0.0};
    double lunar_mean_motion_rad_per_s{0.0};
    double required_lunar_phase_angle_rad{0.0};
    double lunar_soi_radius_meters{0.0};
    double lunar_arrival_speed_at_perilune_m_per_s{0.0};
    double lunar_circular_speed_at_perilune_m_per_s{0.0};
    double nominal_lunar_orbit_insertion_delta_v_m_per_s{0.0};
    bool valid{false};
};

class TransLunarInjectionPlanner {
public:
    [[nodiscard]] TransLunarInjectionPlan plan(
        double parking_orbit_radius_meters,
        double lunar_orbit_radius_meters,
        double lunar_perilune_radius_meters,
        double earth_gravitational_parameter_m3_per_s2,
        double moon_gravitational_parameter_m3_per_s2) const;
};

} // namespace trishula
