#pragma once

#include <cstddef>
#include <vector>

#include "trishula/rover/multi_objective_mission_planner.h"

namespace trishula {

struct RoverDynamicMissionState {
    RoverMissionTarget rover_position{};
    double available_energy_wh{0.0};
    double reserve_energy_wh{0.0};
    std::vector<RoverScienceTarget> remaining_targets{};
    RoverDynamicObstacle obstacle{};
};

struct RoverDynamicReplanResult {
    bool feasible{false};
    std::size_t selected_target_id{0};
    RoverTargetAssessment assessment{};
    double total_objective_score{0.0};
    double energy_after_route_wh{0.0};
    bool mission_priority_changed{false};
};

class RoverDynamicMultiObjectiveMissionPlanner {
public:
    explicit RoverDynamicMultiObjectiveMissionPlanner(RoverMissionWeights weights = {});

    [[nodiscard]] RoverDynamicReplanResult replan(
        const LunarTerrainMap& terrain,
        const RoverDynamicMissionState& state,
        const AutonomousRoverNavigator& navigator,
        double half_extent_m,
        std::size_t previous_target_id = 0U,
        std::size_t newly_prioritized_target_id = 0U) const;

private:
    RoverMissionWeights weights_;
};

} // namespace trishula
