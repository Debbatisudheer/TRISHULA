#pragma once

#include <cstddef>
#include <vector>

#include "trishula/rover/dynamic_multi_objective_mission_planner.h"
#include "trishula/rover/science_operations.h"

namespace trishula {

struct RoverScienceDrivenMissionState {
    RoverMissionTarget current_position{};
    double available_energy_wh{0.0};
    double reserve_energy_wh{0.0};
    std::vector<RoverScienceTarget> remaining_targets{};
    RoverDynamicObstacle obstacle{};
    std::size_t previous_target_id{0U};
};

struct RoverScienceDrivenReplanResult {
    bool feasible{false};
    std::size_t selected_target_id{0U};
    double science_quality{0.0};
    double discovery_bonus{0.0};
    bool mission_priority_changed{false};
    bool newly_discovered_priority{false};
    RoverDynamicReplanResult planner_result{};
};

class RoverScienceDrivenMissionPlanner {
public:
    explicit RoverScienceDrivenMissionPlanner(RoverMissionWeights weights = {});

    [[nodiscard]] RoverScienceDrivenReplanResult replan_after_observation(
        const LunarTerrainMap& terrain,
        const RoverScienceDrivenMissionState& state,
        const AutonomousRoverNavigator& navigator,
        double half_extent_m,
        const RoverScienceObservation& observation,
        std::size_t discovered_priority_target_id = 0U,
        double discovery_bonus = 0.0) const;

private:
    RoverDynamicMultiObjectiveMissionPlanner planner_;
    double discovery_bonus_scale_{1.0};
};

} // namespace trishula
