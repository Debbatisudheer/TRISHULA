#include "trishula/maneuver/trans_lunar_injection.h"

#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kHalf = 0.5;
}

TransLunarInjectionPlan TransLunarInjectionPlanner::plan(
    double parking_orbit_radius_meters,
    double lunar_orbit_radius_meters,
    double lunar_perilune_radius_meters,
    double earth_gravitational_parameter_m3_per_s2,
    double moon_gravitational_parameter_m3_per_s2) const {
    if (parking_orbit_radius_meters <= 0.0 || lunar_orbit_radius_meters <= 0.0 ||
        lunar_perilune_radius_meters <= 0.0) {
        throw std::invalid_argument("TLI radii must be positive");
    }
    if (lunar_orbit_radius_meters <= parking_orbit_radius_meters) {
        throw std::invalid_argument("Lunar orbit radius must exceed parking orbit radius");
    }
    if (earth_gravitational_parameter_m3_per_s2 <= 0.0 ||
        moon_gravitational_parameter_m3_per_s2 <= 0.0) {
        throw std::invalid_argument("Gravitational parameters must be positive");
    }

    const double mu_e = earth_gravitational_parameter_m3_per_s2;
    const double mu_m = moon_gravitational_parameter_m3_per_s2;
    const double r_p = parking_orbit_radius_meters;
    const double r_m = lunar_orbit_radius_meters;
    const double a_t = kHalf * (r_p + r_m);

    TransLunarInjectionPlan plan{};
    plan.parking_orbit_radius_meters = r_p;
    plan.lunar_orbit_radius_meters = r_m;
    plan.transfer_semi_major_axis_meters = a_t;
    plan.parking_orbit_speed_m_per_s = std::sqrt(mu_e / r_p);
    plan.tli_perigee_speed_m_per_s =
        std::sqrt(mu_e * (2.0 / r_p - 1.0 / a_t));
    plan.tli_delta_v_m_per_s =
        plan.tli_perigee_speed_m_per_s - plan.parking_orbit_speed_m_per_s;

    const double transfer_speed_at_lunar_distance =
        std::sqrt(mu_e * (2.0 / r_m - 1.0 / a_t));
    const double lunar_orbital_speed = std::sqrt(mu_e / r_m);
    plan.hyperbolic_excess_speed_m_per_s =
        std::abs(transfer_speed_at_lunar_distance - lunar_orbital_speed);
    plan.characteristic_energy_c3_m2_per_s2 =
        plan.hyperbolic_excess_speed_m_per_s * plan.hyperbolic_excess_speed_m_per_s;

    plan.transfer_time_seconds =
        kPi * std::sqrt((a_t * a_t * a_t) / mu_e);
    plan.lunar_mean_motion_rad_per_s = std::sqrt(mu_e / (r_m * r_m * r_m));

    // At departure the spacecraft is taken as the angular reference. During
    // the transfer the Moon must advance from the departure direction to the
    // transfer-apogee direction. For this simplified circular lunar model,
    // the required initial lead angle is pi - n*t. Wrapped to [0, 2*pi).
    const double raw_phase = kPi - plan.lunar_mean_motion_rad_per_s * plan.transfer_time_seconds;
    plan.required_lunar_phase_angle_rad = std::fmod(raw_phase, 2.0 * kPi);
    if (plan.required_lunar_phase_angle_rad < 0.0) {
        plan.required_lunar_phase_angle_rad += 2.0 * kPi;
    }

    plan.lunar_soi_radius_meters =
        r_m * std::pow(mu_m / mu_e, 2.0 / 5.0);

    plan.lunar_arrival_speed_at_perilune_m_per_s =
        std::sqrt(
            plan.hyperbolic_excess_speed_m_per_s * plan.hyperbolic_excess_speed_m_per_s +
            2.0 * mu_m / lunar_perilune_radius_meters);
    plan.lunar_circular_speed_at_perilune_m_per_s =
        std::sqrt(mu_m / lunar_perilune_radius_meters);
    plan.nominal_lunar_orbit_insertion_delta_v_m_per_s =
        plan.lunar_arrival_speed_at_perilune_m_per_s -
        plan.lunar_circular_speed_at_perilune_m_per_s;
    plan.valid = true;
    return plan;
}

} // namespace trishula
