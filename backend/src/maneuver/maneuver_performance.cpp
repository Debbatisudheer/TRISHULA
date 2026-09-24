#include "trishula/maneuver/maneuver_performance.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {
constexpr double kG0 = 9.80665;
constexpr double kEpsilon = 1e-12;
}

ManeuverPerformanceEstimator::ManeuverPerformanceEstimator(
    double nominal_thrust_newtons,
    double specific_impulse_seconds,
    double gravitational_parameter_m3_per_s2)
    : nominal_thrust_newtons_(nominal_thrust_newtons),
      specific_impulse_seconds_(specific_impulse_seconds),
      gravitational_parameter_m3_per_s2_(gravitational_parameter_m3_per_s2) {
    if (nominal_thrust_newtons <= 0.0 || specific_impulse_seconds <= 0.0 ||
        gravitational_parameter_m3_per_s2 <= 0.0) {
        throw std::invalid_argument("Maneuver performance estimator requires positive propulsion parameters");
    }
}

double ManeuverPerformanceEstimator::nominal_delta_v_for_duration(
    double initial_mass_kg,
    double duration_seconds) const {
    if (initial_mass_kg <= 0.0 || duration_seconds <= 0.0) {
        return 0.0;
    }
    const double mass_flow_rate = nominal_thrust_newtons_ /
        (specific_impulse_seconds_ * kG0);
    const double final_mass = initial_mass_kg - mass_flow_rate * duration_seconds;
    if (final_mass <= 0.0) {
        return 0.0;
    }
    return specific_impulse_seconds_ * kG0 *
        std::log(initial_mass_kg / final_mass);
}

double ManeuverPerformanceEstimator::nominal_delta_v_for_mass_change(
    double initial_mass_kg,
    double final_mass_kg) const {
    if (initial_mass_kg <= 0.0 || final_mass_kg <= 0.0 || final_mass_kg > initial_mass_kg) {
        return 0.0;
    }
    return specific_impulse_seconds_ * kG0 *
        std::log(initial_mass_kg / final_mass_kg);
}

ManeuverPerformanceEstimate ManeuverPerformanceEstimator::estimate(
    const EstimatedState& pre_burn,
    const EstimatedState& post_burn,
    double duration_seconds,
    int direction_sign,
    double initial_mass_kg,
    double final_mass_kg) const {
    return estimate_from_actuator_measurement(
        pre_burn,
        post_burn,
        duration_seconds,
        direction_sign,
        initial_mass_kg,
        final_mass_kg,
        0.0,
        duration_seconds);
}

ManeuverPerformanceEstimate ManeuverPerformanceEstimator::estimate_from_actuator_measurement(
    const EstimatedState& pre_burn,
    const EstimatedState& post_burn,
    double duration_seconds,
    int direction_sign,
    double initial_mass_kg,
    double final_mass_kg,
    double actuator_propulsive_delta_v_m_per_s,
    double nominal_duration_seconds) const {
    ManeuverPerformanceEstimate result{};
    if (duration_seconds <= 0.0 || direction_sign == 0 ||
        initial_mass_kg <= 0.0 || final_mass_kg <= 0.0 ||
        final_mass_kg > initial_mass_kg) {
        return result;
    }

    const Vector3 pre_position = pre_burn.position_meters;
    const Vector3 post_position = post_burn.position_meters;
    const double pre_r = pre_position.magnitude();
    const double post_r = post_position.magnitude();
    if (pre_r <= kEpsilon || post_r <= kEpsilon) {
        return result;
    }

    const Vector3 pre_radius = pre_position / pre_r;
    const Vector3 tangential = pre_burn.velocity_m_per_s -
        pre_radius * pre_burn.velocity_m_per_s.dot(pre_radius);
    if (tangential.magnitude() <= kEpsilon) {
        return result;
    }

    const Vector3 thrust_direction =
        tangential.normalized() * static_cast<double>(direction_sign);

    const Vector3 pre_gravity = pre_radius *
        (-gravitational_parameter_m3_per_s2_ / (pre_r * pre_r));
    const Vector3 post_gravity_direction = post_position / post_r;
    const Vector3 post_gravity = post_gravity_direction *
        (-gravitational_parameter_m3_per_s2_ / (post_r * post_r));

    const Vector3 measured_delta_v = post_burn.velocity_m_per_s - pre_burn.velocity_m_per_s;
    const Vector3 average_gravity = (pre_gravity + post_gravity) * 0.5;
    const Vector3 estimated_propulsive_delta_v =
        measured_delta_v - average_gravity * duration_seconds;

    result.observed_propulsive_delta_v_m_per_s =
        estimated_propulsive_delta_v.dot(thrust_direction);

    result.nominal_propulsive_delta_v_m_per_s =
        nominal_delta_v_for_duration(initial_mass_kg, duration_seconds);
    if (result.nominal_propulsive_delta_v_m_per_s <= kEpsilon) {
        return result;
    }

    if (actuator_propulsive_delta_v_m_per_s > kEpsilon) {
        result.actuator_propulsive_delta_v_m_per_s = actuator_propulsive_delta_v_m_per_s;
        result.effective_thrust_scale =
            actuator_propulsive_delta_v_m_per_s /
            result.nominal_propulsive_delta_v_m_per_s;
    } else {
        // Backward-compatible path for callers that do not have actuator
        // impulse telemetry yet.
        result.actuator_propulsive_delta_v_m_per_s =
            std::abs(result.observed_propulsive_delta_v_m_per_s);
        result.effective_thrust_scale =
            result.actuator_propulsive_delta_v_m_per_s /
            result.nominal_propulsive_delta_v_m_per_s;
    }

    const Vector3 thrust_component =
        thrust_direction * result.observed_propulsive_delta_v_m_per_s;
    result.residual_velocity_m_per_s =
        (estimated_propulsive_delta_v - thrust_component).magnitude();

    const double mass_based_nominal_dv =
        nominal_delta_v_for_mass_change(initial_mass_kg, final_mass_kg);
    result.reconstruction_residual_m_per_s =
        result.actuator_propulsive_delta_v_m_per_s - mass_based_nominal_dv;

    if (nominal_duration_seconds > kEpsilon) {
        result.duration_scale = duration_seconds / nominal_duration_seconds;
    }

    result.valid = std::isfinite(result.effective_thrust_scale) &&
                   std::isfinite(result.duration_scale) &&
                   std::isfinite(result.reconstruction_residual_m_per_s) &&
                   std::isfinite(result.residual_velocity_m_per_s);
    return result;
}

} // namespace trishula
