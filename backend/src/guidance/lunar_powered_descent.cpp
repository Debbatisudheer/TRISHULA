#include "trishula/guidance/lunar_powered_descent.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {

LunarPoweredDescentController::LunarPoweredDescentController(double max_thrust_newtons,
                                                             double dry_mass_kg,
                                                             double lunar_gravity_m_per_s2)
    : max_thrust_newtons_(max_thrust_newtons),
      dry_mass_kg_(dry_mass_kg),
      lunar_gravity_m_per_s2_(lunar_gravity_m_per_s2) {
    if (max_thrust_newtons_ <= 0.0 || dry_mass_kg_ <= 0.0 || lunar_gravity_m_per_s2_ <= 0.0) {
        throw std::invalid_argument("Invalid powered-descent controller parameters");
    }
}

LunarPoweredDescentCommand LunarPoweredDescentController::compute(double altitude_m,
                                                                  double horizontal_position_m,
                                                                  double vertical_velocity_m_per_s,
                                                                  double horizontal_velocity_m_per_s,
                                                                  double mass_kg) const {
    if (altitude_m < 0.0 || mass_kg <= dry_mass_kg_) {
        throw std::invalid_argument("Invalid powered-descent state");
    }

    return computeToTarget(altitude_m, horizontal_position_m, vertical_velocity_m_per_s,
                           horizontal_velocity_m_per_s, mass_kg, 0.0);
}

LunarPoweredDescentCommand LunarPoweredDescentController::computeToTarget(double altitude_m,
                                                                           double horizontal_position_m,
                                                                           double vertical_velocity_m_per_s,
                                                                           double horizontal_velocity_m_per_s,
                                                                           double mass_kg,
                                                                           double target_x_m) const {
    if (altitude_m < 0.0 || mass_kg <= dry_mass_kg_) {
        throw std::invalid_argument("Invalid powered-descent state");
    }

    const bool landing_phase = altitude_m < 2000.0;
    double target_vertical_speed = -std::min(60.0, std::sqrt(2.0 * lunar_gravity_m_per_s2_ * std::max(altitude_m, 0.1) * 0.25));
    if (altitude_m < 2000.0) {
        target_vertical_speed = -std::min(25.0, std::sqrt(2.0 * lunar_gravity_m_per_s2_ * std::max(altitude_m, 0.1) * 0.45));
    }
    if (altitude_m < 200.0) {
        target_vertical_speed = -std::min(5.0, std::sqrt(2.0 * lunar_gravity_m_per_s2_ * std::max(altitude_m, 0.1) * 0.70));
    }

    // Local-vertical guidance: horizontal position/velocity regulation plus
    // vertical braking/flare. The commanded vector is an acceleration request.
    const double position_error = horizontal_position_m - target_x_m;
    double ax = -0.12 * horizontal_velocity_m_per_s - 0.0003 * position_error;
    double az = lunar_gravity_m_per_s2_
              + 1.20 * (target_vertical_speed - vertical_velocity_m_per_s)
              + 0.00002 * altitude_m;

    const double max_accel = max_thrust_newtons_ / mass_kg;
    const double requested = std::hypot(ax, az);
    if (requested > max_accel) {
        const double scale = max_accel / requested;
        ax *= scale;
        az *= scale;
    }

    const double magnitude = std::hypot(ax, az);
    const double throttle = std::clamp(magnitude / max_accel, 0.0, 1.0);
    return {{ax, az, 0.0}, throttle, landing_phase};
}

} // namespace trishula
