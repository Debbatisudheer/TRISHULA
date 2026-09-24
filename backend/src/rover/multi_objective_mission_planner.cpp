#include "trishula/rover/multi_objective_mission_planner.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace trishula {

RoverMultiObjectiveMissionPlanner::RoverMultiObjectiveMissionPlanner(RoverMissionWeights weights)
    : weights_(weights) {
    if (weights_.science_priority < 0.0 || weights_.science_value < 0.0 ||
        weights_.distance_penalty < 0.0 || weights_.terrain_risk_penalty < 0.0 ||
        weights_.energy_penalty < 0.0) {
        throw std::invalid_argument("Mission planner weights must be non-negative");
    }
}

std::vector<RoverTargetAssessment> RoverMultiObjectiveMissionPlanner::assessTargets(
    const LunarTerrainMap& terrain,
    const RoverMissionTarget& start,
    const std::vector<RoverScienceTarget>& targets,
    const AutonomousRoverNavigator& navigator,
    double half_extent_m) const {
    std::vector<RoverTargetAssessment> assessments;
    assessments.reserve(targets.size());

    for (const auto& target : targets) {
        const RoverMissionTarget goal{target.x_m, target.y_m};
        const auto route = navigator.plan(terrain, start, goal, half_extent_m);

        double terrain_risk = 0.0;
        if (route.path_found) {
            for (const auto& node : route.path) {
                const auto sample = terrain.sample(node.x_m);
                terrain_risk += 12.0 * sample.slope + 2.0 * sample.roughness_m;
                terrain_risk += sample.crater ? 50.0 : 0.0;
                terrain_risk += sample.boulder_field ? 70.0 : 0.0;
            }
            terrain_risk /= static_cast<double>(route.path.size());
        }

        const double energy_cost_wh = route.path_found
                                          ? 0.75 * route.path_length_m + target.required_energy_wh
                                          : std::numeric_limits<double>::infinity();
        const double science_score = weights_.science_priority * target.science_priority +
                                      weights_.science_value * target.science_value;
        const double objective = route.path_found
                                     ? science_score -
                                           weights_.distance_penalty * route.path_length_m -
                                           weights_.terrain_risk_penalty * terrain_risk -
                                           weights_.energy_penalty * energy_cost_wh
                                     : -std::numeric_limits<double>::infinity();

        assessments.push_back({target, route, terrain_risk, energy_cost_wh, objective,
                               route.path_found && route.target_safe});
    }

    return assessments;
}

RoverMissionPlan RoverMultiObjectiveMissionPlanner::buildPlan(
    const LunarTerrainMap& terrain,
    const RoverMissionTarget& start,
    const std::vector<RoverScienceTarget>& targets,
    const AutonomousRoverNavigator& navigator,
    double half_extent_m,
    std::size_t max_targets) const {
    RoverMissionPlan plan{};
    if (max_targets == 0U || targets.empty()) return plan;

    RoverMissionTarget current = start;
    std::vector<RoverScienceTarget> remaining = targets;

    for (std::size_t selected = 0; selected < max_targets && !remaining.empty(); ++selected) {
        const auto assessments = assessTargets(terrain, current, remaining, navigator, half_extent_m);
        auto best_it = std::max_element(assessments.begin(), assessments.end(),
            [](const auto& a, const auto& b) { return a.objective_score < b.objective_score; });
        if (best_it == assessments.end() || !best_it->feasible) break;

        plan.feasible = true;
        plan.total_distance_m += best_it->route.path_length_m;
        plan.total_energy_wh += best_it->energy_cost_wh;
        plan.total_science_score += best_it->target.science_priority + best_it->target.science_value;
        plan.ordered_targets.push_back(*best_it);
        current = RoverMissionTarget{best_it->target.x_m, best_it->target.y_m};

        remaining.erase(std::remove_if(remaining.begin(), remaining.end(),
            [&](const auto& target) { return target.id == best_it->target.id; }), remaining.end());
    }

    return plan;
}

} // namespace trishula
