#pragma once

#include <cstddef>

#include "trishula/core/vector3.h"

namespace trishula {

struct LunarOrbitInsertionPlan {
    double moon_gravitational_parameter_m3_s2{0.0};
    double moon_radius_meters{0.0};
    double target_perilune_altitude_meters{0.0};
    double target_apolune_altitude_meters{0.0};
    double pre_burn_speed_m_per_s{0.0};
    double target_circular_speed_m_per_s{0.0};
    double planned_delta_v_m_per_s{0.0};
    double planned_burn_duration_seconds{0.0};
    bool valid{false};
};

struct LunarOrbitInsertionResult {
    LunarOrbitInsertionPlan plan{};
    Vector3 perilune_position_meters{};
    Vector3 perilune_velocity_before_m_per_s{};
    Vector3 perilune_velocity_after_m_per_s{};
    double perilune_radius_meters{0.0};
    double post_burn_specific_energy_j_per_kg{0.0};
    double post_burn_semi_major_axis_meters{0.0};
    double post_burn_eccentricity{0.0};
    double post_burn_perilune_altitude_meters{0.0};
    double post_burn_apolune_altitude_meters{0.0};
    double achieved_delta_v_m_per_s{0.0};
    double midcourse_correction_delta_v_m_per_s{0.0};
    double propellant_consumed_kg{0.0};
    double burn_duration_seconds{0.0};
    std::size_t burn_integration_steps{0};
    bool orbit_captured{false};
};

class LunarOrbitInsertionPlanner {
public:
    [[nodiscard]] LunarOrbitInsertionPlan plan(
        double moon_gravitational_parameter_m3_s2,
        double moon_radius_meters,
        double target_perilune_altitude_meters,
        double target_apolune_altitude_meters,
        double incoming_hyperbolic_excess_m_per_s,
        double thrust_newtons,
        double specific_impulse_seconds,
        double initial_mass_kg,
        double dry_mass_kg) const;
};

[[nodiscard]] double specific_orbital_energy(
    double mu_m3_s2, const Vector3& position_m, const Vector3& velocity_m_per_s);

[[nodiscard]] double semi_major_axis_from_energy(
    double mu_m3_s2, double specific_energy_j_per_kg);

[[nodiscard]] double eccentricity_from_state(
    double mu_m3_s2, const Vector3& position_m, const Vector3& velocity_m_per_s);

[[nodiscard]] double predicted_burn_duration(
    double delta_v_m_per_s, double thrust_newtons, double specific_impulse_seconds,
    double initial_mass_kg, double dry_mass_kg);

}
