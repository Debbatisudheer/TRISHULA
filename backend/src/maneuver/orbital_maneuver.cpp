#include "trishula/maneuver/orbital_maneuver.h"

#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kStandardGravityMPerS2 = 9.80665;
constexpr double kEpsilon = 1e-12;
}

OrbitalManeuverPlan HohmannTransferPlanner::plan(
    double initial_radius_meters,
    double target_radius_meters,
    double gravitational_parameter_m3_per_s2,
    double initial_mass_kg,
    double dry_mass_kg,
    double engine_max_thrust_newtons,
    double engine_specific_impulse_seconds) const {
    if (initial_radius_meters <= 0.0 || target_radius_meters <= 0.0) {
        throw std::invalid_argument("Orbital radii must be positive");
    }
    if (gravitational_parameter_m3_per_s2 <= 0.0) {
        throw std::invalid_argument("Gravitational parameter must be positive");
    }
    if (initial_mass_kg <= 0.0 || dry_mass_kg < 0.0 || initial_mass_kg <= dry_mass_kg) {
        throw std::invalid_argument("Mass must be positive and greater than dry mass");
    }
    if (engine_max_thrust_newtons <= 0.0 || engine_specific_impulse_seconds <= 0.0) {
        throw std::invalid_argument("Engine performance parameters must be positive");
    }

    OrbitalManeuverPlan plan{};
    plan.initial_radius_meters = initial_radius_meters;
    plan.target_radius_meters = target_radius_meters;
    plan.is_raise_maneuver = target_radius_meters > initial_radius_meters + kEpsilon;
    plan.first_burn_direction_sign = plan.is_raise_maneuver ? 1 : (target_radius_meters < initial_radius_meters ? -1 : 0);
    plan.second_burn_direction_sign = plan.first_burn_direction_sign;

    const double mu = gravitational_parameter_m3_per_s2;
    const double r1 = initial_radius_meters;
    const double r2 = target_radius_meters;
    plan.initial_circular_speed_m_per_s = std::sqrt(mu / r1);
    plan.target_circular_speed_m_per_s = std::sqrt(mu / r2);

    if (std::abs(r2 - r1) <= kEpsilon) {
        return plan;
    }

    const double transfer_a = 0.5 * (r1 + r2);
    plan.transfer_semi_major_axis_meters = transfer_a;
    plan.transfer_periapsis_speed_m_per_s = std::sqrt(mu * (2.0 / r1 - 1.0 / transfer_a));
    plan.transfer_apoapsis_speed_m_per_s = std::sqrt(mu * (2.0 / r2 - 1.0 / transfer_a));

    plan.first_burn_delta_v_m_per_s = std::abs(
        plan.transfer_periapsis_speed_m_per_s - plan.initial_circular_speed_m_per_s);
    plan.second_burn_delta_v_m_per_s = std::abs(
        plan.target_circular_speed_m_per_s - plan.transfer_apoapsis_speed_m_per_s);
    plan.total_delta_v_m_per_s =
        plan.first_burn_delta_v_m_per_s + plan.second_burn_delta_v_m_per_s;

    plan.transfer_time_seconds =
        3.14159265358979323846 * std::sqrt((transfer_a * transfer_a * transfer_a) / mu);

    plan.first_burn_duration_seconds = finite_burn_duration(
        plan.first_burn_delta_v_m_per_s,
        initial_mass_kg,
        dry_mass_kg,
        engine_max_thrust_newtons,
        engine_specific_impulse_seconds);

    const double g0 = kStandardGravityMPerS2;
    const double mass_after_first_burn =
        initial_mass_kg * std::exp(-plan.first_burn_delta_v_m_per_s / (engine_specific_impulse_seconds * g0));

    plan.second_burn_duration_seconds = finite_burn_duration(
        plan.second_burn_delta_v_m_per_s,
        mass_after_first_burn,
        dry_mass_kg,
        engine_max_thrust_newtons,
        engine_specific_impulse_seconds);

    return plan;
}

double HohmannTransferPlanner::finite_burn_duration(
    double delta_v_m_per_s,
    double initial_mass_kg,
    double dry_mass_kg,
    double maximum_thrust_newtons,
    double specific_impulse_seconds) {
    if (delta_v_m_per_s <= kEpsilon) {
        return 0.0;
    }
    const double g0 = kStandardGravityMPerS2;
    const double final_mass =
        initial_mass_kg * std::exp(-delta_v_m_per_s / (specific_impulse_seconds * g0));
    if (final_mass < dry_mass_kg - 1e-9) {
        throw std::invalid_argument("Requested maneuver exceeds available propellant");
    }
    const double mass_flow_rate = maximum_thrust_newtons / (specific_impulse_seconds * g0);
    if (mass_flow_rate <= 0.0) {
        throw std::invalid_argument("Engine mass flow rate must be positive");
    }
    return (initial_mass_kg - final_mass) / mass_flow_rate;
}

} // namespace trishula
