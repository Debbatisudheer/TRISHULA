#include <cmath>
#include <iostream>

#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/autonomous_rover_navigation.h"
#include "trishula/rover/closed_loop_rover_navigation.h"
#include "trishula/rover/surface_rover.h"

namespace {
bool require(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main() {
    using namespace trishula;

    LunarTerrainMap terrain(0.0);
    SurfaceRover rover;
    RoverState state;
    AutonomousRoverNavigator planner(2.0, 0.5);
    RoverClosedLoopNavigator navigator(planner);

    const bool deployed = rover.deploy_from_lander(state);
    const bool initialized = rover.initialize_surface_systems(state);
    RoverMissionTarget goal{-200.0, 30.0, 1.5};

    const auto result = navigator.execute(terrain,
                                          rover,
                                          state,
                                          goal,
                                          250.0,
                                          6000,
                                          4.0,
                                          1600,
                                          6.0,
                                          4.0,
                                          0.1);

    bool ok = true;
    ok &= require(deployed, "rover deployment");
    ok &= require(initialized, "surface initialization");
    ok &= require(result.path_found, "initial path found");
    ok &= require(result.replanning_triggered, "deviation triggered replanning");
    ok &= require(result.path_replans > 0, "path replanned");
    ok &= require(result.localization_valid, "re-localization valid");
    ok &= require(!result.hazard_hold, "no terminal hazard hold");
    ok &= require(result.target_reached, "target reached");
    ok &= require(result.final_distance_m <= goal.acceptance_radius_m, "final target accuracy");
    ok &= require(result.control_steps > 0, "closed-loop control executed");
    ok &= require(result.waypoint_advance_count > 0, "waypoints advanced");

    std::cout << "TRISHULA V0.9.37 - Rover Closed-Loop Path Following\n"
              << "==============================================================\n"
              << "  initial path found              : " << (result.path_found ? "YES" : "NO") << "\n"
              << "  control steps                   : " << result.control_steps << "\n"
              << "  waypoints advanced              : " << result.waypoint_advance_count << "\n"
              << "  path replans                    : " << result.path_replans << "\n"
              << "  deviation-triggered replan     : " << (result.replanning_triggered ? "YES" : "NO") << "\n"
              << "  max cross-track error           : " << result.max_cross_track_error_m << " m\n"
              << "  estimated final x               : " << result.estimated_final_x_m << " m\n"
              << "  estimated final y               : " << result.estimated_final_y_m << " m\n"
              << "  final target distance           : " << result.final_distance_m << " m\n"
              << "  re-localization                 : " << (result.localization_valid ? "VALID" : "INVALID") << "\n"
              << "  hazard hold                     : " << (result.hazard_hold ? "YES" : "NO") << "\n"
              << "  A* path following               : ENABLED\n"
              << "  closed-loop deviation recovery : ENABLED\n"
              << "  autonomous replanning           : ENABLED\n"
              << "  closed-loop path following      : " << (ok ? "PASS" : "FAIL") << "\n";

    if (!ok) return 1;
    std::cout << "\nV0.9.37 rover closed-loop path following campaign PASSED.\n";
    return 0;
}
