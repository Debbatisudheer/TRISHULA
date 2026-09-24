#pragma once

#include <cstddef>
#include <vector>

#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/surface_rover.h"

namespace trishula {

struct RoverPathNode {
    double x_m{0.0};
    double y_m{0.0};
};

struct RoverDynamicObstacle {
    double x_m{0.0};
    double y_m{0.0};
    double radius_m{0.0};
    bool active{false};
};

struct RoverNavigationResult {
    bool path_found{false};
    std::size_t expanded_nodes{0};
    double path_length_m{0.0};
    double estimated_cost{0.0};
    bool target_safe{false};
    std::vector<RoverPathNode> path{};
};

class AutonomousRoverNavigator {
public:
    explicit AutonomousRoverNavigator(double grid_resolution_m = 2.0,
                                      double safety_margin_m = 0.5);

    [[nodiscard]] RoverNavigationResult plan(const LunarTerrainMap& terrain,
                                             const RoverMissionTarget& start,
                                             const RoverMissionTarget& goal,
                                             double half_extent_m,
                                             const RoverDynamicObstacle* dynamic_obstacle = nullptr) const;

private:
    double grid_resolution_m_;
    double safety_margin_m_;
};

} // namespace trishula
