#include "trishula/landing/lander_contact_dynamics.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace trishula {
namespace {
constexpr double kEpsilon = 1e-12;
constexpr double kPi = 3.141592653589793238462643383279502884;

Quaternion integrate_orientation(const Quaternion& q, const Vector3& omega, double dt) {
    const Quaternion omega_q{0.0, omega.x, omega.y, omega.z};
    return (q + (q * omega_q) * (0.5 * dt)).normalized();
}
}

LanderContactDynamics::LanderContactDynamics(
    LanderContactParameters parameters,
    LanderContactGeometry geometry,
    TerrainHeightFunction terrain_height)
    : parameters_(parameters),
      geometry_(geometry),
      terrain_height_(terrain_height ? std::move(terrain_height)
                                     : TerrainHeightFunction{[](double, double) { return 0.0; }}) {
    if (parameters_.spring_n_m <= 0.0 || parameters_.damping_n_s_m < 0.0 ||
        parameters_.friction_coefficient < 0.0 || parameters_.max_leg_force_n <= 0.0 ||
        parameters_.ixx_kg_m2 <= 0.0 || parameters_.iyy_kg_m2 <= 0.0 || parameters_.izz_kg_m2 <= 0.0) {
        throw std::invalid_argument("Invalid lander contact dynamics parameters");
    }
}

Vector3 LanderContactDynamics::world_from_body(
    const Quaternion& attitude_body_to_inertial,
    const Vector3& body_vector) {
    return attitude_body_to_inertial.rotate(body_vector);
}

double LanderContactDynamics::tilt_from_vertical_rad(const Quaternion& attitude_body_to_inertial) {
    const Vector3 body_up_world = attitude_body_to_inertial.rotate(Vector3{0.0, 0.0, 1.0});
    return std::acos(std::clamp(body_up_world.z, -1.0, 1.0));
}

LanderContactMetrics LanderContactDynamics::step(LanderContactState& state, double dt_s) const {
    if (dt_s <= 0.0) {
        throw std::invalid_argument("Contact dynamics timestep must be positive");
    }
    if (state.mass_kg <= 0.0) {
        throw std::invalid_argument("Lander mass must be positive");
    }

    LanderContactMetrics metrics{};
    Vector3 net_force{0.0, 0.0, -state.mass_kg * parameters_.lunar_gravity_m_s2};
    Vector3 net_torque{};

    for (std::size_t i = 0; i < geometry_.leg_attach_points_body_m.size(); ++i) {
        const Vector3 r_world = world_from_body(state.attitude_body_to_inertial, geometry_.leg_attach_points_body_m[i]);
        const Vector3 foot_world = state.position_m + r_world;
        const double ground = terrain_height_(foot_world.x, foot_world.y);
        const double penetration = ground - foot_world.z;
        const Vector3 foot_velocity = state.velocity_m_s + state.angular_velocity_rad_s.cross(r_world);

        double normal_force = parameters_.spring_n_m * std::max(0.0, penetration)
            - parameters_.damping_n_s_m * std::min(0.0, foot_velocity.z);
        normal_force = std::clamp(normal_force, 0.0, parameters_.max_leg_force_n);

        metrics.normal_force_n[i] = normal_force;
        metrics.maximum_contact_force_n = std::max(metrics.maximum_contact_force_n, normal_force);

        if (normal_force <= 0.0) {
            continue;
        }

        metrics.in_contact[i] = true;
        const double tangential_speed = std::hypot(foot_velocity.x, foot_velocity.y);
        Vector3 friction_force{};
        if (tangential_speed > kEpsilon) {
            const double friction_limit = parameters_.friction_coefficient * normal_force;
            const double requested = state.mass_kg * tangential_speed / dt_s;
            const double scale = std::min(1.0, friction_limit / (requested + kEpsilon));
            friction_force = {
                -state.mass_kg * foot_velocity.x / dt_s * scale,
                -state.mass_kg * foot_velocity.y / dt_s * scale,
                0.0};
        }

        const Vector3 contact_force = friction_force + Vector3{0.0, 0.0, normal_force};
        net_force += contact_force;
        net_torque += r_world.cross(contact_force);
    }

    const Vector3 acceleration = net_force / state.mass_kg;
    metrics.maximum_shock_acceleration_m_s2 =
        std::max(0.0, acceleration.magnitude() - parameters_.lunar_gravity_m_s2);

    state.position_m += state.velocity_m_s * dt_s + acceleration * (0.5 * dt_s * dt_s);
    state.velocity_m_s += acceleration * dt_s;

    const Vector3 angular_momentum{
        parameters_.ixx_kg_m2 * state.angular_velocity_rad_s.x,
        parameters_.iyy_kg_m2 * state.angular_velocity_rad_s.y,
        parameters_.izz_kg_m2 * state.angular_velocity_rad_s.z};
    const Vector3 gyroscopic = state.angular_velocity_rad_s.cross(angular_momentum);
    const Vector3 angular_acceleration{
        (net_torque.x - gyroscopic.x) / parameters_.ixx_kg_m2,
        (net_torque.y - gyroscopic.y) / parameters_.iyy_kg_m2,
        (net_torque.z - gyroscopic.z) / parameters_.izz_kg_m2};

    state.angular_velocity_rad_s += angular_acceleration * dt_s;
    state.attitude_body_to_inertial =
        integrate_orientation(state.attitude_body_to_inertial, state.angular_velocity_rad_s, dt_s);
    state.time_s += dt_s;

    metrics.tilt_rad = tilt_from_vertical_rad(state.attitude_body_to_inertial);
    metrics.angular_rate_rad_s = state.angular_velocity_rad_s.magnitude();

    const std::size_t contacts = static_cast<std::size_t>(
        std::count(metrics.in_contact.begin(), metrics.in_contact.end(), true));
    const double ground_center = terrain_height_(state.position_m.x, state.position_m.y);
    const bool near_surface = contacts >= 3;
    const double vertical_speed = std::abs(state.velocity_m_s.z);

    if (contacts > 0 && !metrics.touchdown_detected) {
        metrics.touchdown_detected = true;
        metrics.touchdown_time_s = state.time_s;
    }

    metrics.tip_over_detected = metrics.tilt_rad > (15.0 * kPi / 180.0);
    metrics.stable_contact = contacts >= 3 && near_surface && vertical_speed < 0.35 &&
        metrics.angular_rate_rad_s < 0.05 && metrics.tilt_rad < (8.0 * kPi / 180.0);
    metrics.landed_state = metrics.touchdown_detected && metrics.stable_contact && !metrics.tip_over_detected;

    if (state.position_m.z < ground_center) {
        state.position_m.z = ground_center;
        if (state.velocity_m_s.z < 0.0) {
            state.velocity_m_s.z = 0.0;
        }
    }

    return metrics;
}

} // namespace trishula
