#include "trishula/rover/dynamic_multi_objective_mission_planner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace trishula {

RoverDynamicMultiObjectiveMissionPlanner::RoverDynamicMultiObjectiveMissionPlanner(RoverMissionWeights weights)
    : weights_(weights) {
    if (weights_.science_priority < 0.0 || weights_.science_value < 0.0 ||
        weights_.distance_penalty < 0.0 || weights_.terrain_risk_penalty < 0.0 ||
        weights_.energy_penalty < 0.0) {
        throw std::invalid_argument("Mission planner weights must be non-negative");
    }
}

RoverDynamicReplanResult RoverDynamicMultiObjectiveMissionPlanner::replan(
    const LunarTerrainMap& terrain,
    const RoverDynamicMissionState& state,
    const AutonomousRoverNavigator& navigator,
    double half_extent_m,
    std::size_t previous_target_id,
    std::size_t newly_prioritized_target_id) const {
    RoverDynamicReplanResult result{};
    if (state.remaining_targets.empty()) return result;

    double best_score = -std::numeric_limits<double>::infinity();
    bool found = false;

    for (const auto& target : state.remaining_targets) {
        const auto route = navigator.plan(terrain,
                                          state.rover_position,
                                          RoverMissionTarget{target.x_m, target.y_m},
                                          half_extent_m,
                                          state.obstacle.active ? &state.obstacle : nullptr);
        if (!route.path_found || !route.target_safe) continue;

        double terrain_risk = 0.0;
        for (const auto& node : route.path) {
            const auto sample = terrain.sample(node.x_m);
            terrain_risk += 12.0 * sample.slope + 2.0 * sample.roughness_m;
            terrain_risk += sample.crater ? 50.0 : 0.0;
            terrain_risk += sample.boulder_field ? 70.0 : 0.0;
        }
        terrain_risk /= static_cast<double>(route.path.size());

        const double energy_cost = 0.75 * route.path_length_m + target.required_energy_wh;
        const double science_score = weights_.science_priority * target.science_priority +
                                      weights_.science_value * target.science_value;
        const double discovery_bonus = (target.id == newly_prioritized_target_id) ? 50.0 : 0.0;
        const double revisit_penalty = (target.id == previous_target_id) ? 4.0 : 0.0;
        const double objective = science_score + discovery_bonus - revisit_penalty -
                                 weights_.distance_penalty * route.path_length_m -
                                 weights_.terrain_risk_penalty * terrain_risk -
                                 weights_.energy_penalty * energy_cost;

        if (energy_cost > std::max(0.0, state.available_energy_wh - state.reserve_energy_wh)) {
            continue;
        }

        if (!found || objective > best_score) {
            found = true;
            best_score = objective;
            result.selected_target_id = target.id;
            result.assessment = {target, route, terrain_risk, energy_cost, objective, true};
        }
    }

    result.feasible = found;
    if (!found) return result;

    result.total_objective_score = best_score;
    result.energy_after_route_wh = state.available_energy_wh - result.assessment.energy_cost_wh;
    result.mission_priority_changed = newly_prioritized_target_id != 0U &&
                                       result.selected_target_id == newly_prioritized_target_id;
    return result;
}

} // namespace trishula
