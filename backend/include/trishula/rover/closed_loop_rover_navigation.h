#pragma once

#include <cstddef>
#include <vector>

#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/autonomous_rover_navigation.h"
#include "trishula/rover/surface_rover.h"

namespace trishula {

struct RoverClosedLoopResult {
    bool path_found{false};
    bool target_reached{false};
    bool localization_valid{false};
    bool hazard_hold{false};
    bool replanning_triggered{false};
    bool dynamic_obstacle_detected{false};
    bool online_replan_completed{false};
    std::size_t control_steps{0};
    std::size_t path_replans{0};
    std::size_t waypoint_advance_count{0};
    double final_distance_m{0.0};
    double max_cross_track_error_m{0.0};
    double estimated_final_x_m{0.0};
    double estimated_final_y_m{0.0};
};

class RoverClosedLoopNavigator {
public:
    explicit RoverClosedLoopNavigator(AutonomousRoverNavigator planner);

    [[nodiscard]] RoverClosedLoopResult execute(const LunarTerrainMap& terrain,
                                                SurfaceRover& rover,
                                                RoverState& state,
                                                const RoverMissionTarget& goal,
                                                double half_extent_m,
                                                std::size_t max_control_steps,
                                                double deviation_threshold_m,
                                                std::size_t disturbance_step = 0,
                                                double disturbance_x_m = 0.0,
                                                double disturbance_y_m = 0.0,
                                                double dt_s = 0.1,
                                                const RoverDynamicObstacle* dynamic_obstacle = nullptr,
                                                std::size_t obstacle_activation_step = 0) const;

private:
    AutonomousRoverNavigator planner_;

    [[nodiscard]] static double distance_to_polyline(const std::vector<RoverPathNode>& path,
                                                      std::size_t start_index,
                                                      double x_m,
                                                      double y_m);
};

} // namespace trishula
