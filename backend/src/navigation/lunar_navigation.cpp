#include "trishula/navigation/lunar_navigation.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kEpsilon = 1.0e-12;

Vector3 moon_position(double phase, double orbit_radius, double period, double time_s) {
    const double theta = phase + kTwoPi * time_s / period;
    return {orbit_radius * std::cos(theta), orbit_radius * std::sin(theta), 0.0};
}
}

LunarNavigationState LunarNavigator::determine(
    double time_seconds,
    const Vector3& spacecraft_position_meters,
    const Vector3& spacecraft_velocity_m_per_s,
    double moon_gravitational_parameter_m3_per_s2,
    double moon_radius_meters,
    double moon_orbit_radius_meters,
    double moon_orbit_period_seconds,
    double moon_phase_rad) const {
    if (time_seconds < 0.0 || moon_gravitational_parameter_m3_per_s2 <= 0.0 ||
        moon_radius_meters <= 0.0 || moon_orbit_radius_meters <= 0.0 ||
        moon_orbit_period_seconds <= 0.0) {
        throw std::invalid_argument("Invalid lunar navigation inputs");
    }

    const Vector3 moon_position_m = moon_position(
        moon_phase_rad, moon_orbit_radius_meters, moon_orbit_period_seconds, time_seconds);
    const Vector3 relative_position = spacecraft_position_meters - moon_position_m;
    const double distance = relative_position.magnitude();
    if (distance <= kEpsilon) {
        throw std::invalid_argument("Lunar navigation requires non-zero Moon-relative position");
    }

    // The simplified Moon model is circular, so the Moon's inertial velocity is
    // analytically available and should be removed before computing Moon-relative
    // approach energy and geometry.
    const double omega = kTwoPi / moon_orbit_period_seconds;
    const Vector3 moon_velocity_m_per_s{
        -moon_orbit_radius_meters * omega * std::sin(
            moon_phase_rad + omega * time_seconds),
        moon_orbit_radius_meters * omega * std::cos(
            moon_phase_rad + omega * time_seconds),
        0.0};
    const Vector3 relative_velocity = spacecraft_velocity_m_per_s - moon_velocity_m_per_s;
    const double speed = relative_velocity.magnitude();
    const double radial_velocity = relative_position.dot(relative_velocity) / distance;
    const double tangential_speed = std::sqrt(std::max(0.0, speed * speed - radial_velocity * radial_velocity));
    const double specific_energy = 0.5 * speed * speed - moon_gravitational_parameter_m3_per_s2 / distance;
    const double c3 = 2.0 * specific_energy;
    const double hyperbolic_excess = c3 > 0.0 ? std::sqrt(c3) : 0.0;
    const double flight_path_angle = std::atan2(radial_velocity, tangential_speed);
    const double perilune_speed = std::sqrt(
        std::max(0.0, hyperbolic_excess * hyperbolic_excess +
            2.0 * moon_gravitational_parameter_m3_per_s2 / distance));

    LunarNavigationState result{};
    result.time_seconds = time_seconds;
    result.moon_position_meters = moon_position_m;
    result.spacecraft_position_meters = spacecraft_position_meters;
    result.relative_position_meters = relative_position;
    result.relative_velocity_m_per_s = relative_velocity;
    result.moon_distance_meters = distance;
    result.altitude_above_moon_surface_meters = distance - moon_radius_meters;
    result.relative_speed_m_per_s = speed;
    result.radial_velocity_m_per_s = radial_velocity;
    result.flight_path_angle_rad = flight_path_angle;
    result.specific_orbital_energy_j_per_kg = specific_energy;
    result.characteristic_energy_c3_m2_per_s2 = c3;
    result.hyperbolic_excess_speed_m_per_s = hyperbolic_excess;
    result.lunar_perilune_speed_m_per_s = perilune_speed;
    result.hyperbolic_approach = c3 > 0.0;
    return result;
}

} // namespace trishula
