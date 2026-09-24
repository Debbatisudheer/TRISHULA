#pragma once

#include <cstddef>
#include <vector>

#include "trishula/rover/autonomous_rover_navigation.h"

namespace trishula {

struct RoverScienceTarget {
    std::size_t id{0};
    double x_m{0.0};
    double y_m{0.0};
    double science_priority{0.0};
    double science_value{0.0};
    double required_energy_wh{0.0};
};

struct RoverMissionWeights {
    double science_priority{1.0};
    double science_value{1.0};
    double distance_penalty{0.01};
    double terrain_risk_penalty{1.0};
    double energy_penalty{0.02};
};

struct RoverTargetAssessment {
    RoverScienceTarget target{};
    RoverNavigationResult route{};
    double terrain_risk{0.0};
    double energy_cost_wh{0.0};
    double objective_score{0.0};
    bool feasible{false};
};

struct RoverMissionPlan {
    bool feasible{false};
    std::vector<RoverTargetAssessment> ordered_targets{};
    double total_distance_m{0.0};
    double total_energy_wh{0.0};
    double total_science_score{0.0};
};

class RoverMultiObjectiveMissionPlanner {
public:
    explicit RoverMultiObjectiveMissionPlanner(RoverMissionWeights weights = {});

    [[nodiscard]] std::vector<RoverTargetAssessment> assessTargets(
        const LunarTerrainMap& terrain,
        const RoverMissionTarget& start,
        const std::vector<RoverScienceTarget>& targets,
        const AutonomousRoverNavigator& navigator,
        double half_extent_m) const;

    [[nodiscard]] RoverMissionPlan buildPlan(
        const LunarTerrainMap& terrain,
        const RoverMissionTarget& start,
        const std::vector<RoverScienceTarget>& targets,
        const AutonomousRoverNavigator& navigator,
        double half_extent_m,
        std::size_t max_targets) const;

private:
    RoverMissionWeights weights_;
};

} // namespace trishula
