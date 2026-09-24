#pragma once

#include "trishula/guidance/lunar_powered_descent.h"
#include "trishula/navigation/terrain_relative_navigation.h"

namespace trishula {

struct TerrainRelativePoweredDescentOutput {
    LunarPoweredDescentCommand command{};
    TerrainEstimate landing_site{};
    double terrain_relative_altitude_m{0.0};
    double estimated_x_m{0.0};
    bool target_updated{false};
};

// Closed-loop landing guidance adapter. It connects noisy terrain sensing and
// terrain-relative estimation directly to the powered-descent controller.
class TerrainRelativePoweredDescentGuidance {
public:
    TerrainRelativePoweredDescentGuidance(TerrainRelativeSensor& sensor,
                                           TerrainRelativeNavigator& navigator,
                                           const LunarPoweredDescentController& descent_controller,
                                           double scan_half_width_m,
                                           double scan_spacing_m,
                                           double replanning_period_s = 5.0,
                                           double target_lock_altitude_m = 3000.0);

    [[nodiscard]] TerrainRelativePoweredDescentOutput compute(double vehicle_altitude_m,
                                                               double horizontal_position_m,
                                                               double vertical_velocity_m_per_s,
                                                               double horizontal_velocity_m_per_s,
                                                               double mass_kg,
                                                               double simulation_time_s);

    [[nodiscard]] bool has_target() const { return has_target_; }
    [[nodiscard]] TerrainEstimate current_target() const { return target_; }
    [[nodiscard]] std::size_t target_update_count() const { return target_update_count_; }

private:
    void update_target(double vehicle_altitude_m, double horizontal_position_m);

    TerrainRelativeSensor& sensor_;
    TerrainRelativeNavigator& navigator_;
    const LunarPoweredDescentController& descent_controller_;
    double scan_half_width_m_;
    double scan_spacing_m_;
    double replanning_period_s_;
    double target_lock_altitude_m_;
    double last_plan_time_s_{-1.0};
    double planning_center_x_m_{0.0};
    bool planning_center_initialized_{false};
    TerrainEstimate target_{};
    bool has_target_{false};
    std::size_t target_update_count_{0};
};

} // namespace trishula
