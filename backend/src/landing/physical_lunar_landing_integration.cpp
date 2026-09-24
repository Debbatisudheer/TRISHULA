#include "trishula/landing/physical_lunar_landing_integration.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace trishula {
namespace {

constexpr double kEpsilon = 1.0e-12;
constexpr double kStandardGravityMPerS2 = 9.80665;

struct LocalFrame {
    Vector3 east{};
    Vector3 north{};
    Vector3 up{};
};

LocalFrame make_local_frame(const Vector3& relative_position) {
    const Vector3 up = relative_position.normalized();

    Vector3 reference{0.0, 0.0, 1.0};
    if (std::abs(up.z) > 0.95) {
        reference = {1.0, 0.0, 0.0};
    }

    Vector3 east = reference.cross(up);
    if (east.magnitude() <= kEpsilon) {
        throw std::runtime_error("Unable to construct lunar local frame");
    }
    east = east.normalized();
    const Vector3 north = up.cross(east).normalized();
    return {east, north, up};
}

double dot_column(const Vector3& column, const Vector3& vector) {
    return column.dot(vector);
}

Quaternion quaternion_from_basis(const LocalFrame& frame) {
    // Body axes are +X=east, +Y=north, +Z=up.
    const double m00 = frame.east.x;
    const double m01 = frame.north.x;
    const double m02 = frame.up.x;
    const double m10 = frame.east.y;
    const double m11 = frame.north.y;
    const double m12 = frame.up.y;
    const double m20 = frame.east.z;
    const double m21 = frame.north.z;
    const double m22 = frame.up.z;

    const double trace = m00 + m11 + m22;
    Quaternion q{};
    if (trace > 0.0) {
        const double s = 2.0 * std::sqrt(trace + 1.0);
        q.w = 0.25 * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const double s = 2.0 * std::sqrt(1.0 + m00 - m11 - m22);
        q.w = (m21 - m12) / s;
        q.x = 0.25 * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const double s = 2.0 * std::sqrt(1.0 + m11 - m00 - m22);
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25 * s;
        q.z = (m12 + m21) / s;
    } else {
        const double s = 2.0 * std::sqrt(1.0 + m22 - m00 - m11);
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25 * s;
    }
    return q.normalized();
}

} // namespace

PhysicalLunarLandingIntegrationExecutor::PhysicalLunarLandingIntegrationExecutor(
    PhysicalLunarLandingIntegrationConfiguration configuration,
    EphemerisFunction moon_position,
    EphemerisFunction moon_velocity,
    double moon_radius_meters)
    : configuration_(configuration),
      moon_position_(std::move(moon_position)),
      moon_velocity_(std::move(moon_velocity)),
      moon_radius_meters_(moon_radius_meters) {
    if (configuration_.contact_start_altitude_meters <= 0.0 ||
        configuration_.control_step_seconds <= 0.0 ||
        configuration_.maximum_contact_duration_seconds <= 0.0 ||
        configuration_.maximum_touchdown_speed_m_per_s <= 0.0 ||
        configuration_.maximum_landed_tilt_rad <= 0.0 ||
        configuration_.maximum_landed_angular_rate_rad_s <= 0.0 ||
        configuration_.minimum_contact_legs < 1 ||
        configuration_.minimum_contact_legs > 4 ||
        moon_radius_meters_ <= 0.0 ||
        !moon_position_ || !moon_velocity_) {
        throw std::invalid_argument("Invalid physical lunar landing integration configuration");
    }
}

Vector3 PhysicalLunarLandingIntegrationExecutor::relative_position(
    const SimulationEngine& simulation) const {
    return simulation.spacecraft().state().position_meters -
        moon_position_(simulation.clock().time());
}

Vector3 PhysicalLunarLandingIntegrationExecutor::relative_velocity(
    const SimulationEngine& simulation) const {
    return simulation.spacecraft().state().velocity_m_per_s -
        moon_velocity_(simulation.clock().time());
}

PhysicalLunarLandingIntegrationResult PhysicalLunarLandingIntegrationExecutor::execute(
    SimulationEngine& simulation) const {
    PhysicalLunarLandingIntegrationResult result{};

    const TrueState initial = simulation.spacecraft().state();
    const Vector3 initial_relative_position = relative_position(simulation);
    const Vector3 initial_relative_velocity = relative_velocity(simulation);

    const double initial_radius = initial_relative_position.magnitude();
    if (initial_radius <= moon_radius_meters_) {
        throw std::runtime_error("Physical landing integration requires positive lunar altitude");
    }

    result.initial_altitude_meters = initial_radius - moon_radius_meters_;
    result.initial_vertical_velocity_m_per_s =
        initial_relative_position.dot(initial_relative_velocity) / initial_radius;

    if (result.initial_altitude_meters > configuration_.contact_start_altitude_meters) {
        throw std::runtime_error(
            "Current physical state is above the V0.9.48 surface-contact boundary");
    }

    result.valid_initial_state = true;

    // Derive the local landing frame directly from the actual V0.9.47
    // Moon-relative state. No synthetic position or velocity is introduced.
    const LocalFrame frame = make_local_frame(initial_relative_position);
    const LunarTerrainMap terrain(
        dot_column(frame.east, initial_relative_position));

    LanderContactState contact_state{};
    contact_state.position_m = {
        dot_column(frame.east, initial_relative_position),
        dot_column(frame.north, initial_relative_position),
        result.initial_altitude_meters
    };
    contact_state.velocity_m_s = {
        dot_column(frame.east, initial_relative_velocity),
        dot_column(frame.north, initial_relative_velocity),
        result.initial_vertical_velocity_m_per_s
    };
    contact_state.attitude_body_to_inertial = quaternion_from_basis(frame);
    contact_state.angular_velocity_rad_s = {
        dot_column(frame.east, initial.angular_velocity_rad_s),
        dot_column(frame.north, initial.angular_velocity_rad_s),
        dot_column(frame.up, initial.angular_velocity_rad_s)
    };
    contact_state.mass_kg = initial.mass_kg;

    // V0.9.47 provides the physical terminal-descent state, but that state
    // can still contain substantial horizontal and radial velocity. V0.9.48
    // therefore performs an explicit finite-thrust terminal braking segment
    // using the actual engine thrust/Isp/dry-mass model before handing the
    // state to the landing-leg contact dynamics.
    const double maximum_thrust = simulation.main_engine().maximum_thrust_newtons();
    const double specific_impulse = simulation.main_engine().specific_impulse_seconds();
    const double dry_mass = simulation.main_engine().dry_mass_kg();
    const double lunar_gravity = configuration_.contact_parameters.lunar_gravity_m_s2;

    double powered_time = 0.0;
    const double powered_dt = std::min(0.02, configuration_.control_step_seconds);
    const double powered_limit = configuration_.maximum_contact_duration_seconds;

    while (powered_time < powered_limit) {
        const double ground = terrain.elevation(contact_state.position_m.x);
        const double clearance = contact_state.position_m.z - ground;

        if (clearance <= 2.0 &&
            std::abs(contact_state.velocity_m_s.z) <= 1.5 &&
            std::hypot(contact_state.velocity_m_s.x, contact_state.velocity_m_s.y) <= 1.5) {
            break;
        }

        const double target_vertical_velocity =
            std::clamp(-0.8 - 0.25 * (clearance - 2.0), -3.0, -0.5);

        const Vector3 desired_velocity{
            -0.25 * contact_state.velocity_m_s.x,
            -0.25 * contact_state.velocity_m_s.y,
            target_vertical_velocity
        };

        const Vector3 velocity_error = desired_velocity - contact_state.velocity_m_s;
        Vector3 commanded_acceleration = velocity_error * 0.8;
        commanded_acceleration.z += lunar_gravity;

        const double mass = contact_state.mass_kg;
        if (mass <= dry_mass + 1.0e-9) {
            throw std::runtime_error(
                "V0.9.48 terminal braking exhausted the physical propellant reserve");
        }

        const double required_thrust = mass * commanded_acceleration.magnitude();
        const double throttle =
            std::clamp(required_thrust / maximum_thrust, 0.0, 1.0);
        const double actual_thrust = maximum_thrust * throttle;
        if (actual_thrust <= kEpsilon) {
            break;
        }

        const Vector3 thrust_acceleration =
            commanded_acceleration.magnitude() > kEpsilon
                ? commanded_acceleration.normalized() * (actual_thrust / mass)
                : Vector3{};

        const Vector3 acceleration =
            thrust_acceleration - Vector3{0.0, 0.0, lunar_gravity};

        const double consumed =
            std::min(mass - dry_mass,
                     actual_thrust /
                         (specific_impulse * kStandardGravityMPerS2) *
                         powered_dt);

        contact_state.position_m +=
            contact_state.velocity_m_s * powered_dt +
            acceleration * (0.5 * powered_dt * powered_dt);
        contact_state.velocity_m_s += acceleration * powered_dt;
        contact_state.mass_kg -= consumed;
        contact_state.time_s += powered_dt;
        powered_time += powered_dt;
    }

    result.terminal_descent_boundary_reached =
        contact_state.position_m.z <= configuration_.contact_start_altitude_meters;

    LanderContactDynamics contact_dynamics(
        configuration_.contact_parameters,
        configuration_.contact_geometry,
        [&terrain](double x, double) { return terrain.elevation(x); });

    LanderContactMetrics metrics{};
    const std::uint64_t max_steps = static_cast<std::uint64_t>(
        std::ceil(configuration_.maximum_contact_duration_seconds /
                  configuration_.control_step_seconds));

    for (std::uint64_t step = 0; step < max_steps; ++step) {
        metrics = contact_dynamics.step(
            contact_state,
            configuration_.control_step_seconds);

        result.physical_contact_steps = step + 1;
        result.maximum_contact_force_newtons =
            std::max(result.maximum_contact_force_newtons,
                     metrics.maximum_contact_force_n);
        result.maximum_shock_acceleration_m_per_s2 =
            std::max(result.maximum_shock_acceleration_m_per_s2,
                     metrics.maximum_shock_acceleration_m_s2);

        if (metrics.touchdown_detected && !result.touchdown_detected) {
            result.touchdown_detected = true;
            result.touchdown_speed_m_per_s = contact_state.velocity_m_s.magnitude();
        }

        if (metrics.landed_state) {
            result.stable_contact = metrics.stable_contact;
            result.landed_state = true;
            result.tip_over_detected = metrics.tip_over_detected;
            break;
        }
    }

    result.duration_seconds = powered_time +
        static_cast<double>(result.physical_contact_steps) *
            configuration_.control_step_seconds;
    result.final_altitude_meters = contact_state.position_m.z;
    result.final_vertical_velocity_m_per_s = contact_state.velocity_m_s.z;
    result.final_tilt_rad = metrics.tilt_rad;
    result.final_angular_rate_rad_s = metrics.angular_rate_rad_s;
    result.tip_over_detected = metrics.tip_over_detected;

    if (result.touchdown_detected &&
        result.touchdown_speed_m_per_s > configuration_.maximum_touchdown_speed_m_per_s) {
        result.landed_state = false;
        result.stable_contact = false;
    }

    const double final_time = simulation.clock().time() + contact_state.time_s;
    const Vector3 moon_final = moon_position_(final_time);
    const Vector3 moon_velocity_final = moon_velocity_(final_time);

    result.final_inertial_position_meters =
        moon_final +
        frame.east * contact_state.position_m.x +
        frame.north * contact_state.position_m.y +
        frame.up * (moon_radius_meters_ + contact_state.position_m.z);

    result.final_inertial_velocity_m_per_s =
        moon_velocity_final +
        frame.east * contact_state.velocity_m_s.x +
        frame.north * contact_state.velocity_m_s.y +
        frame.up * contact_state.velocity_m_s.z;

    return result;
}

} // namespace trishula
