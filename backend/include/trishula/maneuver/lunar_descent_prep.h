#pragma once

namespace trishula {

struct LunarDescentPreparationPlan {
    double moon_gravitational_parameter_m3_s2{0.0};
    double moon_radius_meters{0.0};
    double initial_perilune_altitude_meters{0.0};
    double initial_apolune_altitude_meters{0.0};
    double target_perilune_altitude_meters{0.0};
    double target_apolune_altitude_meters{0.0};
    double first_burn_delta_v_m_per_s{0.0};
    double second_burn_delta_v_m_per_s{0.0};
    double total_delta_v_m_per_s{0.0};
    double landing_interface_altitude_meters{0.0};
    bool valid{false};
};

class LunarDescentPreparationPlanner {
public:
    LunarDescentPreparationPlan plan(
        double moon_mu,
        double moon_radius,
        double initial_perilune_altitude,
        double initial_apolune_altitude,
        double target_perilune_altitude,
        double target_apolune_altitude) const;
};

} // namespace trishula
