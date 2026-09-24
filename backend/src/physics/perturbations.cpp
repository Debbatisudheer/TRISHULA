#include "trishula/physics/perturbations.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kAtmosphereVelocityFloor = 1.0e-9;

Vector3 circular_orbit_position(double radius, double period, double phase, double time_s) {
    const double theta = phase + kTwoPi * time_s / period;
    return {radius * std::cos(theta), radius * std::sin(theta), 0.0};
}
}

PerturbationModel::PerturbationModel(PerturbationConfiguration configuration)
    : configuration_(configuration) {
    if (configuration_.j2 < 0.0) throw std::invalid_argument("J2 must be non-negative");
    if (configuration_.body_rotation_rate_rad_s < 0.0) throw std::invalid_argument("Body rotation rate must be non-negative");
    if (configuration_.drag_coefficient < 0.0) throw std::invalid_argument("Drag coefficient must be non-negative");
    if (configuration_.reference_area_m2 < 0.0) throw std::invalid_argument("Reference area must be non-negative");
    if (configuration_.reference_density_kg_m3 < 0.0) throw std::invalid_argument("Reference density must be non-negative");
    if (configuration_.density_scale_height_m <= 0.0) throw std::invalid_argument("Density scale height must be positive");
    if (configuration_.moon_gravitational_parameter_m3_s2 <= 0.0 || configuration_.moon_orbit_radius_m <= 0.0 || configuration_.moon_orbit_period_s <= 0.0) {
        throw std::invalid_argument("Moon parameters must be positive");
    }
    if (configuration_.solar_pressure_n_m2 < 0.0 || configuration_.solar_reflectivity_coefficient < 0.0 || configuration_.solar_area_m2 < 0.0) {
        throw std::invalid_argument("Solar radiation pressure parameters must be non-negative");
    }
    if (configuration_.solar_orbit_radius_m <= 0.0 || configuration_.solar_orbit_period_s <= 0.0) {
        throw std::invalid_argument("Solar orbit parameters must be positive");
    }
}

Vector3 PerturbationModel::j2_acceleration(const CelestialBody& body, const Vector3& r) const {
    if (!configuration_.enable_j2) return {};
    const double radius = r.magnitude();
    if (radius <= body.radius()) return {};
    const double mu = body.gravitational_parameter();
    const double re = body.radius();
    const double z2_over_r2 = (r.z * r.z) / (radius * radius);
    const double factor = 1.5 * configuration_.j2 * mu * re * re / std::pow(radius, 5.0);
    return {
        factor * r.x * (5.0 * z2_over_r2 - 1.0),
        factor * r.y * (5.0 * z2_over_r2 - 1.0),
        factor * r.z * (5.0 * z2_over_r2 - 3.0)};
}

Vector3 PerturbationModel::atmospheric_drag_acceleration(const CelestialBody& body, const TrueState& state) const {
    if (!configuration_.enable_atmospheric_drag || state.mass_kg <= 0.0) return {};
    const Vector3 r = state.position_meters - body.center_position();
    const double altitude = r.magnitude() - body.radius();
    if (altitude < 0.0) return {};
    const double density = configuration_.reference_density_kg_m3 *
        std::exp(-(altitude - configuration_.reference_altitude_m) / configuration_.density_scale_height_m);
    const Vector3 body_rotation_velocity = Vector3{0.0, 0.0, configuration_.body_rotation_rate_rad_s}.cross(r);
    const Vector3 relative_velocity = state.velocity_m_per_s - body_rotation_velocity;
    const double speed = relative_velocity.magnitude();
    if (speed < kAtmosphereVelocityFloor) return {};
    const double magnitude = 0.5 * density * speed * speed *
        configuration_.drag_coefficient * configuration_.reference_area_m2 / state.mass_kg;
    return relative_velocity.normalized() * (-magnitude);
}

Vector3 PerturbationModel::third_body_moon_acceleration(const CelestialBody& body, const Vector3& r, double time_seconds) const {
    if (!configuration_.enable_third_body_moon) return {};
    const Vector3 moon = circular_orbit_position(
        configuration_.moon_orbit_radius_m,
        configuration_.moon_orbit_period_s,
        configuration_.moon_phase_rad,
        time_seconds);
    const Vector3 body_to_moon = moon - body.center_position();
    const Vector3 spacecraft_to_moon = body_to_moon - r;
    const double d_sc = spacecraft_to_moon.magnitude();
    const double d_body = body_to_moon.magnitude();
    if (d_sc <= 0.0 || d_body <= 0.0) return {};
    return spacecraft_to_moon * (configuration_.moon_gravitational_parameter_m3_s2 / std::pow(d_sc, 3.0))
         - body_to_moon * (configuration_.moon_gravitational_parameter_m3_s2 / std::pow(d_body, 3.0));
}

Vector3 PerturbationModel::solar_radiation_pressure_acceleration(const TrueState& state, double time_seconds) const {
    if (!configuration_.enable_solar_radiation_pressure || state.mass_kg <= 0.0) return {};
    const Vector3 sun_position = circular_orbit_position(
        configuration_.solar_orbit_radius_m,
        configuration_.solar_orbit_period_s,
        configuration_.solar_phase_rad,
        time_seconds);
    const Vector3 from_sun = state.position_meters - sun_position;
    const double distance = from_sun.magnitude();
    if (distance <= 0.0) return {};
    const double pressure = configuration_.solar_pressure_n_m2 *
        std::pow(configuration_.solar_orbit_radius_m / distance, 2.0);
    const double magnitude = pressure * configuration_.solar_reflectivity_coefficient *
        configuration_.solar_area_m2 / state.mass_kg;
    return from_sun.normalized() * magnitude;
}

Vector3 PerturbationModel::acceleration(const CelestialBody& body, const TrueState& state, double time_seconds) const {
    const Vector3 r = state.position_meters - body.center_position();
    return j2_acceleration(body, r) +
        atmospheric_drag_acceleration(body, state) +
        third_body_moon_acceleration(body, r, time_seconds) +
        solar_radiation_pressure_acceleration(state, time_seconds);
}

const PerturbationConfiguration& PerturbationModel::configuration() const noexcept { return configuration_; }

} // namespace trishula
