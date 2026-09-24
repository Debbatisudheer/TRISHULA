#include "trishula/maneuver/lunar_orbit_insertion.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kG0 = 9.80665;
constexpr double kEpsilon = 1e-12;
}


double specific_orbital_energy(double mu_m3_s2, const Vector3& position_m, const Vector3& velocity_m_per_s) {
    const double radius = position_m.magnitude();
    if (mu_m3_s2 <= 0.0 || radius <= kEpsilon) throw std::invalid_argument("Invalid state for orbital energy");
    return 0.5 * velocity_m_per_s.magnitude_squared() - mu_m3_s2 / radius;
}

double semi_major_axis_from_energy(double mu, double energy) {
    if (mu <= 0.0 || std::abs(energy) <= kEpsilon) throw std::invalid_argument("Specific energy must be non-zero");
    return -mu / (2.0 * energy);
}

double eccentricity_from_state(double mu, const Vector3& r, const Vector3& v) {
    const double radius = r.magnitude();
    if (mu <= 0.0 || radius <= kEpsilon) throw std::invalid_argument("Invalid state for eccentricity");
    const Vector3 h = r.cross(v);
    const Vector3 e_vec = v.cross(h) / mu - r / radius;
    return e_vec.magnitude();
}

double predicted_burn_duration(double dv, double thrust, double isp, double initial_mass, double dry_mass) {
    if (dv < 0.0 || thrust <= 0.0 || isp <= 0.0 || initial_mass <= dry_mass || dry_mass < 0.0) {
        throw std::invalid_argument("Invalid burn inputs");
    }
    if (dv <= kEpsilon) return 0.0;
    const double final_mass = initial_mass * std::exp(-dv / (isp * kG0));
    if (final_mass <= dry_mass) throw std::runtime_error("LOI requires more propellant than available");
    const double mdot = thrust / (isp * kG0);
    return (initial_mass - final_mass) / mdot;
}

LunarOrbitInsertionPlan LunarOrbitInsertionPlanner::plan(
    double moon_mu, double moon_radius, double perilune_altitude, double apolune_altitude,
    double v_inf, double thrust, double isp, double initial_mass, double dry_mass) const {
    if (moon_mu <= 0.0 || moon_radius <= 0.0 || perilune_altitude < 0.0 ||
        apolune_altitude <= perilune_altitude || v_inf < 0.0) {
        throw std::invalid_argument("Invalid lunar orbit insertion geometry");
    }
    const double rp = moon_radius + perilune_altitude;
    const double ra = moon_radius + apolune_altitude;
    const double a = 0.5 * (rp + ra);
    const double vp_hyp = std::sqrt(v_inf * v_inf + 2.0 * moon_mu / rp);
    const double vp_target = std::sqrt(moon_mu * (2.0 / rp - 1.0 / a));
    const double dv = std::max(0.0, vp_hyp - vp_target);
    LunarOrbitInsertionPlan out{};
    out.moon_gravitational_parameter_m3_s2 = moon_mu;
    out.moon_radius_meters = moon_radius;
    out.target_perilune_altitude_meters = perilune_altitude;
    out.target_apolune_altitude_meters = apolune_altitude;
    out.pre_burn_speed_m_per_s = vp_hyp;
    out.target_circular_speed_m_per_s = vp_target;
    out.planned_delta_v_m_per_s = dv;
    out.planned_burn_duration_seconds = predicted_burn_duration(dv, thrust, isp, initial_mass, dry_mass);
    out.valid = true;
    return out;
}

} // namespace trishula
