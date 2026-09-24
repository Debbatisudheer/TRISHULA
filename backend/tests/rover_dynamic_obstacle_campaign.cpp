#include <iostream>
#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/autonomous_rover_navigation.h"
#include "trishula/rover/closed_loop_rover_navigation.h"
#include "trishula/rover/surface_rover.h"

namespace { bool require(bool c,const char* m){ if(!c) std::cerr<<"FAIL: "<<m<<'\n'; return c; } }

int main(){
    using namespace trishula;
    LunarTerrainMap terrain(0.0);
    SurfaceRover rover;
    RoverState state;
    const bool deployed = rover.deploy_from_lander(state);
    const bool initialized = rover.initialize_surface_systems(state);
    AutonomousRoverNavigator planner(2.0,0.5);
    RoverClosedLoopNavigator navigator(planner);
    RoverMissionTarget goal{-200.0,30.0,1.5};
    RoverDynamicObstacle obstacle{-50.0,22.0,10.0,true};
    const auto result = navigator.execute(terrain, rover, state, goal, 250.0, 6500, 4.0,
                                          0,0.0,0.0,0.1,&obstacle,900);
    bool ok=true;
    ok &= require(deployed,"rover deployment");
    ok &= require(initialized,"surface initialization");
    ok &= require(result.path_found,"initial path found");
    ok &= require(result.dynamic_obstacle_detected,"dynamic obstacle detected");
    ok &= require(result.online_replan_completed,"online replan completed");
    ok &= require(result.path_replans>0,"path replanned around dynamic obstacle");
    ok &= require(result.target_reached,"target reached after obstacle");
    ok &= require(!result.hazard_hold,"no terminal hazard hold");
    ok &= require(result.final_distance_m <= goal.acceptance_radius_m,"final target accuracy");
    std::cout << "TRISHULA V0.9.38 - Dynamic Obstacle Detection + Online Replanning\n"
              << "==============================================================\n"
              << "  initial path found              : " << (result.path_found?"YES":"NO") << "\n"
              << "  dynamic obstacle activated      : YES\n"
              << "  dynamic obstacle detected       : " << (result.dynamic_obstacle_detected?"YES":"NO") << "\n"
              << "  path replans                    : " << result.path_replans << "\n"
              << "  online replan completed         : " << (result.online_replan_completed?"YES":"NO") << "\n"
              << "  final target distance            : " << result.final_distance_m << " m\n"
              << "  hazard hold                     : " << (result.hazard_hold?"YES":"NO") << "\n"
              << "  autonomous online replanning   : " << ((ok)?"PASS":"FAIL") << "\n";
    if(!ok) return 1;
    std::cout << "\nV0.9.38 dynamic obstacle + online replanning campaign PASSED.\n";
    return 0;
}
