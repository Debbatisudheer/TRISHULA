#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/autonomous_rover_navigation.h"
#include "trishula/rover/dynamic_multi_objective_mission_planner.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    using namespace trishula;

    LunarTerrainMap terrain(0.0);
    AutonomousRoverNavigator navigator(2.0, 0.5);
    RoverDynamicMultiObjectiveMissionPlanner planner({1.2, 1.0, 0.015, 1.5, 0.02});

    const std::vector<RoverScienceTarget> targets{
        {1, -100.0, 0.0, 8.0, 6.0, 80.0},
        {2, 220.0, 60.0, 9.0, 8.0, 120.0},
        {3, -200.0, -40.0, 8.0, 7.0, 130.0},
        {4, 1250.0, 0.0, 18.0, 16.0, 220.0},
    };

    RoverDynamicMissionState initial{};
    initial.rover_position = {0.0, 0.0};
    initial.available_energy_wh = 1000.0;
    initial.reserve_energy_wh = 120.0;
    initial.remaining_targets = targets;

    const auto first = planner.replan(terrain, initial, navigator, 1800.0);
    if (!first.feasible || first.selected_target_id == 0U) return 1;

    RoverDynamicMissionState disturbed = initial;
    disturbed.rover_position = {80.0, 20.0};
    disturbed.available_energy_wh = 620.0;
    disturbed.obstacle = {0.0, 0.0, 34.0, true};

    const auto second = planner.replan(terrain, disturbed, navigator, 1800.0, first.selected_target_id, 3U);
    if (!second.assessment.route.path_found) return 3;
    if (!(second.energy_after_route_wh >= disturbed.reserve_energy_wh)) return 4;

    const bool priority_changed = second.selected_target_id == 3U;
    if (!priority_changed) return 5;

    RoverDynamicMissionState degraded = disturbed;
    degraded.available_energy_wh = 250.0;
    const auto third = planner.replan(terrain, degraded, navigator, 1800.0, first.selected_target_id, 3U);
    if (third.feasible && third.energy_after_route_wh < degraded.reserve_energy_wh) return 6;

    std::cout << "TRISHULA V0.9.40 - Dynamic Multi-Objective Mission Replanning\n"
              << "===================================================================\n"
              << "  initial target selected         : " << first.selected_target_id << "\n"
              << "  dynamic obstacle active         : YES\n"
              << "  online replan target            : " << second.selected_target_id << "\n"
              << "  mission priority changed        : " << (priority_changed ? "YES" : "NO") << "\n"
              << "  online path found               : " << (second.assessment.route.path_found ? "YES" : "NO") << "\n"
              << "  replanned path distance         : " << second.assessment.route.path_length_m << " m\n"
              << "  replanned energy                : " << second.assessment.energy_cost_wh << " Wh\n"
              << "  remaining energy                : " << second.energy_after_route_wh << " Wh\n"
              << "  battery reserve respected       : YES\n"
              << "  low-energy target filtering     : " << (third.feasible ? "ACTIVE" : "PASS") << "\n"
              << "  dynamic mission replanning     : PASS\n\n"
              << "V0.9.40 dynamic multi-objective mission replanning campaign PASSED.\n";
    return 0;
}
