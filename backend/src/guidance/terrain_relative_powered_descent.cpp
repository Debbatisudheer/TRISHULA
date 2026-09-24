#include "trishula/guidance/terrain_relative_powered_descent.h"

#include <algorithm>
#include <stdexcept>

namespace trishula {

TerrainRelativePoweredDescentGuidance::TerrainRelativePoweredDescentGuidance(
    TerrainRelativeSensor& sensor,
    TerrainRelativeNavigator& navigator,
    const LunarPoweredDescentController& descent_controller,
    double scan_half_width_m,
    double scan_spacing_m,
    double replanning_period_s,
    double target_lock_altitude_m)
    : sensor_(sensor),
      navigator_(navigator),
      descent_controller_(descent_controller),
      scan_half_width_m_(scan_half_width_m),
      scan_spacing_m_(scan_spacing_m),
      replanning_period_s_(replanning_period_s),
      target_lock_altitude_m_(target_lock_altitude_m) {
    if (scan_half_width_m_ <= 0.0 || scan_spacing_m_ <= 0.0 ||
        replanning_period_s_ <= 0.0 || target_lock_altitude_m_ <= 0.0) {
        throw std::invalid_argument("Invalid terrain-relative landing guidance parameters");
    }
}

void TerrainRelativePoweredDescentGuidance::update_target(double vehicle_altitude_m,
                                                           double horizontal_position_m) {
    if (!planning_center_initialized_) {
        planning_center_x_m_ = horizontal_position_m;
        planning_center_initialized_ = true;
    }
    const auto measurements = sensor_.scan(vehicle_altitude_m,
                                            planning_center_x_m_,
                                            scan_half_width_m_,
                                            scan_spacing_m_);
    const auto estimates = navigator_.estimate(measurements, scan_spacing_m_);
    target_ = navigator_.select_safe_site(estimates);
    has_target_ = true;
    ++target_update_count_;
}

TerrainRelativePoweredDescentOutput TerrainRelativePoweredDescentGuidance::compute(
    double vehicle_altitude_m,
    double horizontal_position_m,
    double vertical_velocity_m_per_s,
    double horizontal_velocity_m_per_s,
    double mass_kg,
    double simulation_time_s) {
    if (vehicle_altitude_m < 0.0) {
        throw std::invalid_argument("Vehicle altitude must be non-negative");
    }

    const bool target_locked = has_target_ && vehicle_altitude_m < target_lock_altitude_m_;
    const bool should_replan = !has_target_ ||
        (!target_locked && (last_plan_time_s_ < 0.0 ||
                            simulation_time_s - last_plan_time_s_ >= replanning_period_s_));
    bool target_updated = false;
    if (should_replan) {
        update_target(vehicle_altitude_m, horizontal_position_m);
        last_plan_time_s_ = simulation_time_s;
        target_updated = true;
    }

    const auto current_measurement = sensor_.measure(vehicle_altitude_m, horizontal_position_m);
    double terrain_relative_altitude = current_measurement.valid
        ? current_measurement.terrain_relative_range_m
        : vehicle_altitude_m - target_.elevation_m;
    terrain_relative_altitude = std::max(0.0, terrain_relative_altitude);
    const double estimated_x = horizontal_position_m;

    const auto command = descent_controller_.computeToTarget(
        terrain_relative_altitude,
        estimated_x,
        vertical_velocity_m_per_s,
        horizontal_velocity_m_per_s,
        mass_kg,
        target_.x_m);

    return {command, target_, terrain_relative_altitude, estimated_x, target_updated};
}

} // namespace trishula
