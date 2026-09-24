#include "trishula/navigation/navigation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace trishula {

namespace {
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kEpsilon = 1e-12;

double clamp_unit(double value) {
    return std::clamp(value, -1.0, 1.0);
}

double positive_or_zero(double value) {
    return value > 0.0 ? value : 0.0;
}
}

NavigationState EarthCenteredNavigator::determine(
    const EstimatedState& estimated_state,
    const CelestialBody& primary_body) const {
    const Vector3 r = estimated_state.position_meters - primary_body.center_position();
    const Vector3 v = estimated_state.velocity_m_per_s;
    const double r_mag = r.magnitude();
    const double v_mag = v.magnitude();
    if (r_mag <= kEpsilon) {
        throw std::invalid_argument("Navigation requires non-zero position relative to primary body");
    }

    const double mu = primary_body.gravitational_parameter();
    const Vector3 h_vec = r.cross(v);
    const double h_mag = h_vec.magnitude();
    if (h_mag <= kEpsilon) {
        throw std::invalid_argument("Navigation requires non-zero specific angular momentum");
    }

    const Vector3 node_vec{-h_vec.y, h_vec.x, 0.0};
    const double node_mag = node_vec.magnitude();
    const Vector3 e_vec = (v.cross(h_vec) / mu) - (r / r_mag);
    const double eccentricity = e_vec.magnitude();

    const double specific_energy = 0.5 * v.magnitude_squared() - mu / r_mag;
    double semi_major_axis = std::numeric_limits<double>::infinity();
    if (std::abs(specific_energy) > kEpsilon) {
        semi_major_axis = -mu / (2.0 * specific_energy);
    }

    const double inclination = std::acos(clamp_unit(h_vec.z / h_mag));

    double raan = 0.0;
    if (node_mag > kEpsilon) {
        raan = std::atan2(node_vec.y, node_vec.x);
        if (raan < 0.0) {
            raan += 2.0 * kPi;
        }
    }

    double argument_of_periapsis = 0.0;
    if (node_mag > kEpsilon && eccentricity > kEpsilon) {
        const double cosine = clamp_unit(node_vec.dot(e_vec) / (node_mag * eccentricity));
        argument_of_periapsis = std::acos(cosine);
        if (e_vec.z < 0.0) {
            argument_of_periapsis = 2.0 * kPi - argument_of_periapsis;
        }
    }

    double true_anomaly = 0.0;
    if (eccentricity > kEpsilon) {
        const double cosine = clamp_unit(e_vec.dot(r) / (eccentricity * r_mag));
        true_anomaly = std::acos(cosine);
        if (r.dot(v) < 0.0) {
            true_anomaly = 2.0 * kPi - true_anomaly;
        }
    } else if (node_mag > kEpsilon) {
        const double cosine = clamp_unit(node_vec.dot(r) / (node_mag * r_mag));
        true_anomaly = std::acos(cosine);
        if (r.z < 0.0) {
            true_anomaly = 2.0 * kPi - true_anomaly;
        }
    }

    const double radial_velocity = r.dot(v) / r_mag;
    double flight_path_angle = std::atan2(radial_velocity,
                                          std::sqrt(positive_or_zero(v_mag * v_mag - radial_velocity * radial_velocity)));

    double period = 0.0;
    if (semi_major_axis > 0.0 && eccentricity < 1.0) {
        period = 2.0 * kPi * std::sqrt((semi_major_axis * semi_major_axis * semi_major_axis) / mu);
    }

    NavigationState result{};
    result.time_seconds = estimated_state.time_seconds;
    result.position_meters = estimated_state.position_meters;
    result.velocity_m_per_s = estimated_state.velocity_m_per_s;
    result.radial_distance_meters = r_mag;
    result.altitude_meters = r_mag - primary_body.radius();
    result.speed_m_per_s = v_mag;
    result.radial_velocity_m_per_s = radial_velocity;
    result.flight_path_angle_rad = flight_path_angle;
    result.orbital_period_seconds = period;
    result.orbit = {
        h_mag,
        eccentricity,
        semi_major_axis,
        inclination,
        raan,
        argument_of_periapsis,
        true_anomaly,
        specific_energy,
        !(semi_major_axis > 0.0 && eccentricity < 1.0)
    };
    return result;
}

} // namespace trishula
