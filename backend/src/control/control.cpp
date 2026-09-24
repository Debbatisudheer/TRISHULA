#include "trishula/control/control.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {

namespace {
constexpr double kEpsilon = 1e-12;

Vector3 clamp_magnitude(const Vector3& v, double limit) {
    const double magnitude = v.magnitude();
    if (magnitude <= limit || magnitude <= kEpsilon) {
        return v;
    }
    return v * (limit / magnitude);
}

Vector3 clamp_direction_cone(const Vector3& direction_body, double max_angle_rad) {
    const Vector3 direction = direction_body.magnitude() > kEpsilon ? direction_body.normalized() : Vector3{1.0, 0.0, 0.0};
    const double max_cos = std::cos(max_angle_rad);
    const double axial = direction.x;
    if (axial >= max_cos) return direction;
    Vector3 lateral{0.0, direction.y, direction.z};
    if (lateral.magnitude() <= kEpsilon) return {max_cos, std::sin(max_angle_rad), 0.0};
    lateral = lateral.normalized();
    const double sin_angle = std::sin(max_angle_rad);
    return (Vector3{max_cos, 0.0, 0.0} + lateral * sin_angle).normalized();
}

Quaternion rotation_from_to(const Vector3& from, const Vector3& to) {
    const double from_mag = from.magnitude();
    const double to_mag = to.magnitude();
    if (from_mag <= kEpsilon || to_mag <= kEpsilon) {
        return {};
    }

    const Vector3 a = from / from_mag;
    const Vector3 b = to / to_mag;
    const double dot = std::clamp(a.dot(b), -1.0, 1.0);

    if (dot > 1.0 - 1e-10) {
        return {};
    }

    if (dot < -1.0 + 1e-10) {
        Vector3 axis = a.cross({0.0, 0.0, 1.0});
        if (axis.magnitude() <= kEpsilon) {
            axis = a.cross({0.0, 1.0, 0.0});
        }
        axis = axis.normalized();
        return {0.0, axis.x, axis.y, axis.z};
    }

    const Vector3 cross = a.cross(b);
    Quaternion q{1.0 + dot, cross.x, cross.y, cross.z};
    return q.normalized();
}

Vector3 quaternion_error_vector_body(const Quaternion& current,
                                     const Quaternion& target) {
    Quaternion error = current.inverse() * target;
    if (error.w < 0.0) {
        error = error * -1.0;
    }
    return {2.0 * error.x, 2.0 * error.y, 2.0 * error.z};
}
}

BaselineAttitudeThrustController::BaselineAttitudeThrustController(
    double attitude_gain,
    double rate_damping,
    double max_torque_nm,
    double max_gimbal_angle_rad,
    double rcs_force_limit_newtons,
    double rcs_only_acceleration_threshold_m_per_s2)
    : attitude_gain_(attitude_gain),
      rate_damping_(rate_damping),
      max_torque_nm_(max_torque_nm),
      max_gimbal_angle_rad_(max_gimbal_angle_rad),
      rcs_force_limit_newtons_(rcs_force_limit_newtons),
      rcs_only_acceleration_threshold_m_per_s2_(rcs_only_acceleration_threshold_m_per_s2) {
    if (attitude_gain_ < 0.0 || rate_damping_ < 0.0) {
        throw std::invalid_argument("Control gains cannot be negative");
    }
    if (max_torque_nm_ <= 0.0 || max_gimbal_angle_rad_ <= 0.0 || rcs_force_limit_newtons_ <= 0.0 || rcs_only_acceleration_threshold_m_per_s2_ < 0.0) {
        throw std::invalid_argument("Control limits must be positive");
    }
}

ControlCommand BaselineAttitudeThrustController::compute(
    const EstimatedState& estimated_state,
    const GuidanceCommand& guidance,
    const Vector3& estimated_gravity_acceleration_m_per_s2,
    double vehicle_mass_kg,
    const MainEngine& engine,
    const RcsModule& rcs) const {
    if (vehicle_mass_kg <= 0.0 || rcs.maximum_force_newtons() <= 0.0) {
        throw std::invalid_argument("Controller requires positive vehicle mass");
    }

    const Vector3 requested_thrust =
        guidance.commanded_acceleration_m_per_s2 * vehicle_mass_kg;
    const double thrust_magnitude = requested_thrust.magnitude();

    Quaternion target_attitude = estimated_state.attitude_body_to_inertial;
    Vector3 commanded_thrust_direction_body{1.0, 0.0, 0.0};
    double gimbal_angle = 0.0;
    double thrust_alignment_angle = 0.0;
    if (thrust_magnitude > kEpsilon) {
        target_attitude = rotation_from_to({1.0, 0.0, 0.0}, requested_thrust).normalized();
        const Vector3 desired_direction_body =
            estimated_state.attitude_body_to_inertial.inverse().rotate(requested_thrust).normalized();
        const double desired_cosine = std::clamp(desired_direction_body.x, -1.0, 1.0);
        thrust_alignment_angle = std::acos(desired_cosine);
        commanded_thrust_direction_body = clamp_direction_cone(desired_direction_body, max_gimbal_angle_rad_);
        const double cosine = std::clamp(commanded_thrust_direction_body.x, -1.0, 1.0);
        gimbal_angle = std::acos(cosine);
    }

    double throttle = std::clamp(
        thrust_magnitude / engine.maximum_thrust_newtons(), 0.0, 1.0);
    // A fixed-direction main engine should not fire when the body is outside its gimbal envelope.
    if (thrust_alignment_angle > max_gimbal_angle_rad_ + 1e-9) {
        throttle = 0.0;
    }

    const double requested_acceleration = requested_thrust.magnitude() / vehicle_mass_kg;
    Vector3 attitude_error_body{};
    Vector3 torque{};
    Vector3 rcs_force_body{};

    // Fine orbital corrections are handled entirely by a three-axis RCS cluster.
    // The vehicle holds its current attitude during fine translation instead of
    // slewing the main-engine thrust axis for tiny corrections.
    if (requested_acceleration <= rcs_only_acceleration_threshold_m_per_s2_) {
        target_attitude = estimated_state.attitude_body_to_inertial;
        commanded_thrust_direction_body = {1.0, 0.0, 0.0};
        throttle = 0.0;
        thrust_alignment_angle = 0.0;
        gimbal_angle = 0.0;
        const Vector3 requested_rcs_body =
            estimated_state.attitude_body_to_inertial.inverse().rotate(requested_thrust);
        rcs_force_body = clamp_magnitude(requested_rcs_body, rcs_force_limit_newtons_);
        // No attitude maneuver is commanded for fine RCS translation.
    } else {
        attitude_error_body =
            quaternion_error_vector_body(estimated_state.attitude_body_to_inertial, target_attitude);
        torque = attitude_error_body * attitude_gain_ -
                 estimated_state.angular_velocity_rad_s * rate_damping_;
        torque = clamp_magnitude(torque, max_torque_nm_);
    }

    // estimated_gravity is intentionally accepted as part of the controller contract.
    // V0.8 controls the guidance correction acceleration itself; gravity remains an
    // independent physical force in the dynamics model.
    (void)estimated_gravity_acceleration_m_per_s2;

    ControlCommand command{};
    command.throttle = throttle;
    command.commanded_body_torque_nm = torque;
    command.target_attitude_body_to_inertial = target_attitude;
    command.requested_thrust_inertial_newtons = requested_thrust;
    command.commanded_thrust_direction_body = commanded_thrust_direction_body;
    command.commanded_rcs_force_body_newtons = rcs_force_body;
    command.thrust_vector_gimbal_angle_rad = gimbal_angle;
    command.attitude_error_body = attitude_error_body;
    return command;
}

} // namespace trishula
