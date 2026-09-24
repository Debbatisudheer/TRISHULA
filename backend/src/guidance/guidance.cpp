#include "trishula/guidance/guidance.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {

namespace {
constexpr double kEpsilon = 1e-12;

Vector3 normalized_or_zero(const Vector3& v) {
    const double magnitude = v.magnitude();
    if (magnitude <= kEpsilon) {
        return {};
    }
    return v / magnitude;
}

Vector3 tangential_direction(const Vector3& position, const Vector3& velocity) {
    const Vector3 radial = normalized_or_zero(position);
    const Vector3 tangential_component = velocity - radial * velocity.dot(radial);
    const double magnitude = tangential_component.magnitude();
    if (magnitude <= kEpsilon) {
        return {};
    }
    return tangential_component / magnitude;
}

Vector3 clamp_magnitude(const Vector3& v, double limit) {
    const double magnitude = v.magnitude();
    if (magnitude <= limit || magnitude <= kEpsilon) {
        return v;
    }
    return v * (limit / magnitude);
}
}

BaselineOrbitalGuidance::BaselineOrbitalGuidance(
    GuidanceTarget target,
    double radial_gain_per_s2,
    double radial_velocity_damping_per_s,
    double tangential_gain_per_s,
    double acceleration_limit_m_per_s2)
    : target_(target),
      radial_gain_per_s2_(radial_gain_per_s2),
      radial_velocity_damping_per_s_(radial_velocity_damping_per_s),
      tangential_gain_per_s_(tangential_gain_per_s),
      acceleration_limit_m_per_s2_(acceleration_limit_m_per_s2) {
    if (target_.target_altitude_meters < 0.0) {
        throw std::invalid_argument("Target altitude cannot be negative");
    }
    if (target_.target_tangential_speed_m_per_s <= 0.0) {
        throw std::invalid_argument("Target tangential speed must be positive");
    }
    if (radial_gain_per_s2_ < 0.0 || radial_velocity_damping_per_s_ < 0.0 || tangential_gain_per_s_ < 0.0) {
        throw std::invalid_argument("Guidance gains cannot be negative");
    }
    if (acceleration_limit_m_per_s2_ <= 0.0) {
        throw std::invalid_argument("Acceleration limit must be positive");
    }
}

GuidanceCommand BaselineOrbitalGuidance::compute(
    const NavigationState& navigation,
    const CelestialBody& primary_body) const {
    const Vector3 position = navigation.position_meters - primary_body.center_position();
    const Vector3 velocity = navigation.velocity_m_per_s;
    const double radius = position.magnitude();
    if (radius <= kEpsilon) {
        throw std::invalid_argument("Guidance requires non-zero position relative to primary body");
    }

    const Vector3 radial = position / radius;
    const Vector3 tangent = tangential_direction(position, velocity);
    const double target_radius = primary_body.radius() + target_.target_altitude_meters;

    // Baseline orbital guidance: drive radial and tangential errors toward a requested orbit.
    // This is intentionally transparent and bounded; it is not a flight-qualified optimal-control law.
    const double radial_error = target_radius - radius;
    const double radial_velocity = velocity.dot(radial);
    const double tangential_speed = velocity.dot(tangent);
    const double tangential_error = target_.target_tangential_speed_m_per_s - tangential_speed;

    Vector3 desired_acceleration =
        radial * (radial_gain_per_s2_ * radial_error - radial_velocity_damping_per_s_ * radial_velocity) +
        tangent * (tangential_gain_per_s_ * tangential_error);
    desired_acceleration = clamp_magnitude(desired_acceleration, acceleration_limit_m_per_s2_);

    GuidanceCommand command{};
    command.desired_position_meters = primary_body.center_position() + radial * target_radius;
    command.desired_velocity_m_per_s = tangent * target_.target_tangential_speed_m_per_s;
    command.commanded_acceleration_m_per_s2 = desired_acceleration;
    command.radial_error_meters = radial_error;
    command.tangential_speed_error_m_per_s = tangential_error;
    command.radial_velocity_m_per_s = radial_velocity;
    command.target_radius_meters = target_radius;
    return command;
}

const GuidanceTarget& BaselineOrbitalGuidance::target() const noexcept {
    return target_;
}

} // namespace trishula
