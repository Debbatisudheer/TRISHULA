#include "trishula/rover/science_driven_mission_planner.h"

#include <algorithm>
#include <stdexcept>

namespace trishula {

RoverScienceDrivenMissionPlanner::RoverScienceDrivenMissionPlanner(RoverMissionWeights weights)
    : planner_(weights) {
    if (weights.science_value < 0.0 || weights.science_priority < 0.0) {
        throw std::invalid_argument("Science-driven mission weights must be non-negative");
    }
}

RoverScienceDrivenReplanResult RoverScienceDrivenMissionPlanner::replan_after_observation(
    const LunarTerrainMap& terrain,
    const RoverScienceDrivenMissionState& state,
    const AutonomousRoverNavigator& navigator,
    double half_extent_m,
    const RoverScienceObservation& observation,
    std::size_t discovered_priority_target_id,
    double discovery_bonus) const {
    RoverScienceDrivenReplanResult result{};
    if (state.remaining_targets.empty()) return result;

    const bool valid_discovery = observation.acquired && observation.validated && observation.stored &&
                                 observation.quality > 0.0 && discovered_priority_target_id != 0U;
    const double applied_bonus = valid_discovery ? std::max(0.0, discovery_bonus) : 0.0;

    result.discovery_bonus = applied_bonus;
    result.newly_discovered_priority = valid_discovery;

    auto replanning_targets = state.remaining_targets;
    if (valid_discovery) {
        for (auto& target : replanning_targets) {
            if (target.id == discovered_priority_target_id) {
                // Convert a validated science discovery into a deterministic mission-priority update.
                target.science_priority += applied_bonus;
                break;
            }
        }
    }

    result.planner_result = planner_.replan(
        terrain,
        RoverDynamicMissionState{
            state.current_position,
            state.available_energy_wh,
            state.reserve_energy_wh,
            replanning_targets,
            state.obstacle},
        navigator,
        half_extent_m,
        state.previous_target_id,
        valid_discovery ? discovered_priority_target_id : 0U);

    if (!result.planner_result.feasible) return result;

    result.feasible = true;
    result.selected_target_id = result.planner_result.selected_target_id;
    result.science_quality = observation.quality;
    result.mission_priority_changed = result.planner_result.mission_priority_changed || valid_discovery;
    return result;
}

} // namespace trishula
