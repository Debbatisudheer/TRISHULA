#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/autonomous_rover_navigation.h"
#include "trishula/rover/multi_objective_mission_planner.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace trishula;

    LunarTerrainMap terrain(0.0);
    AutonomousRoverNavigator navigator(2.0, 0.5);
    RoverMultiObjectiveMissionPlanner planner({1.2, 1.0, 0.015, 1.5, 0.015});

    const RoverMissionTarget start{0.0, 0.0};
    const std::vector<RoverScienceTarget> targets{
        {1, -100.0, 0.0, 8.0, 6.0, 80.0},
        {2, 220.0, 60.0, 9.0, 8.0, 120.0},
        {3, 700.0, -40.0, 14.0, 12.0, 160.0},
        {4, 1250.0, 0.0, 18.0, 16.0, 220.0}, // crater region -> should be rejected as unsafe
    };

    const auto assessments = planner.assessTargets(terrain, start, targets, navigator, 1800.0);
    if (assessments.size() != targets.size()) return 1;

    bool saw_unsafe = false;
    bool saw_safe = false;
    for (const auto& assessment : assessments) {
        saw_unsafe = saw_unsafe || !assessment.feasible;
        saw_safe = saw_safe || assessment.feasible;
    }
    if (!saw_safe || !saw_unsafe) return 2;

    const auto mission = planner.buildPlan(terrain, start, targets, navigator, 1800.0, 3);
    if (!mission.feasible || mission.ordered_targets.empty() || mission.ordered_targets.size() > 3U) return 3;
    if (!(mission.total_distance_m > 0.0) || !(mission.total_energy_wh > 0.0)) return 4;
    if (!std::isfinite(mission.ordered_targets.front().objective_score)) return 5;

    std::cout << "TRISHULA V0.9.39 - Rover Multi-Objective Mission Planning\n"
              << "==============================================================\n"
              << "  targets assessed                : " << assessments.size() << "\n"
              << "  safe targets                    : "
              << static_cast<std::size_t>(std::count_if(assessments.begin(), assessments.end(),
                   [](const auto& a) { return a.feasible; })) << "\n"
              << "  unsafe targets                  : "
              << static_cast<std::size_t>(std::count_if(assessments.begin(), assessments.end(),
                   [](const auto& a) { return !a.feasible; })) << "\n"
              << "  selected mission targets        : " << mission.ordered_targets.size() << "\n"
              << "  total planned distance          : " << mission.total_distance_m << " m\n"
              << "  total estimated energy          : " << mission.total_energy_wh << " Wh\n"
              << "  total science score             : " << mission.total_science_score << "\n"
              << "  objective-aware selection       : PASS\n"
              << "  distance/risk/energy tradeoff  : ENABLED\n"
              << "  unsafe-target rejection         : PASS\n"
              << "  mission sequencing              : PASS\n"
              << "  multi-objective mission planning: PASS\n\n"
              << "V0.9.39 rover multi-objective mission planning campaign PASSED.\n";

    return 0;
}
