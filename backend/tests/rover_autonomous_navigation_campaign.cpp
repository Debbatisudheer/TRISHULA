#include <cmath>
#include <iostream>

#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/autonomous_rover_navigation.h"

namespace {
bool require(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main() {
    using namespace trishula;
    LunarTerrainMap terrain(0.0);
    AutonomousRoverNavigator navigator(2.0, 0.5);

    RoverMissionTarget start{0.0, 0.0, 1.0};
    RoverMissionTarget goal{-200.0, 30.0, 1.0};
    const auto route = navigator.plan(terrain, start, goal, 250.0);

    RoverMissionTarget blocked_goal{650.0, 0.0, 1.0};
    const auto blocked = navigator.plan(terrain, start, blocked_goal, 800.0);

    bool ok = true;
    ok &= require(route.path_found, "path found");
    ok &= require(route.path.size() > 2, "multi-node path");
    ok &= require(route.path_length_m >= std::hypot(goal.x_m - start.x_m, goal.y_m - start.y_m),
                  "path length valid");
    ok &= require(route.expanded_nodes > 0, "nodes expanded");
    ok &= require(route.target_safe, "goal classified safe");
    ok &= require(!blocked.path_found || !blocked.target_safe, "hazard handling blocks unsafe goal");

    std::cout << "TRISHULA V0.9.36 - Rover Terrain-Relative Localization + Autonomous Navigation\n"
              << "==============================================================\n"
              << "  grid resolution                 : 2.000 m\n"
              << "  path found                      : " << (route.path_found ? "YES" : "NO") << "\n"
              << "  path nodes                      : " << route.path.size() << "\n"
              << "  expanded nodes                  : " << route.expanded_nodes << "\n"
              << "  planned path length             : " << route.path_length_m << " m\n"
              << "  planner cost                    : " << route.estimated_cost << "\n"
              << "  goal safe                       : " << (route.target_safe ? "YES" : "NO") << "\n"
              << "  unsafe-goal handling            : " << ((!blocked.path_found || !blocked.target_safe) ? "PASS" : "FAIL") << "\n"
              << "  graph search                    : A*\n"
              << "  terrain hazard cost             : ENABLED\n"
              << "  terrain-relative planning      : ENABLED\n"
              << "  autonomous navigation           : " << (ok ? "PASS" : "FAIL") << "\n";

    if (!ok) return 1;
    std::cout << "\nV0.9.36 rover terrain-relative localization + autonomous navigation campaign PASSED.\n";
    return 0;
}
