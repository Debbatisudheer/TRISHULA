#include <cmath>
#include <iostream>

#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/surface_rover.h"

namespace {

bool require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

}

int main() {
    using namespace trishula;

    LunarTerrainMap terrain(0.0);
    SurfaceRover rover;
    RoverState state;

    const bool deployed = rover.deploy_from_lander(state);
    const bool initialized = rover.initialize_surface_systems(state);

    RoverMissionTarget target{-200.0, 0.0, 1.5};
    const auto metrics = rover.drive_to_target(state, terrain, target, 6000, 0.1);

    bool ok = true;
    ok &= require(deployed, "rover deployment");
    ok &= require(initialized, "surface initialization");
    ok &= require(metrics.localization_valid, "surface localization");
    ok &= require(state.mode == RoverMode::MissionComplete, "rover mission completion");
    ok &= require(metrics.target_reached, "target reached");
    ok &= require(metrics.distance_to_target_m <= target.acceptance_radius_m, "target accuracy");

    RoverState hazard_state{};
    ok &= require(rover.deploy_from_lander(hazard_state), "second rover deployment");
    ok &= require(rover.initialize_surface_systems(hazard_state), "second rover initialization");
    RoverMissionTarget hazard_target{650.0, 0.0, 1.5};
    hazard_state.heading_rad = 0.0;
    const auto hazard_metrics = rover.drive_to_target(hazard_state, terrain, hazard_target, 15000, 0.1);
    ok &= require(hazard_state.mode == RoverMode::HazardHold, "hazard hold triggered");
    ok &= require(hazard_metrics.hazard_detected, "hazard detection");

    RoverState fault_state{};
    fault_state.battery_soc = 0.10;
    ok &= require(!rover.deploy_from_lander(fault_state), "low-power deployment inhibited");
    ok &= require(fault_state.mode == RoverMode::Fault, "fault state entered");

    std::cout << "TRISHULA V0.9.35 - Rover Deployment + Surface Mobility\n"
              << "==============================================================\n"
              << "  rover deployed                  : " << (deployed ? "YES" : "NO") << "\n"
              << "  rover surface systems ready     : " << (initialized ? "YES" : "NO") << "\n"
              << "  target x                         : " << target.x_m << " m\n"
              << "  final rover x                    : " << state.x_m << " m\n"
              << "  final rover y                    : " << state.y_m << " m\n"
              << "  final target distance            : " << metrics.distance_to_target_m << " m\n"
              << "  localization valid               : " << (metrics.localization_valid ? "YES" : "NO") << "\n"
              << "  target reached                   : " << (metrics.target_reached ? "YES" : "NO") << "\n"
              << "  hazard hold scenario             : " << (hazard_state.mode == RoverMode::HazardHold ? "PASS" : "FAIL") << "\n"
              << "  fault inhibition scenario        : " << (fault_state.mode == RoverMode::Fault ? "PASS" : "FAIL") << "\n"
              << "  wheel/drive dynamics             : ENABLED\n"
              << "  surface localization             : ENABLED\n"
              << "  obstacle/hazard handling         : ENABLED\n"
              << "  autonomous mobility              : " << (ok ? "PASS" : "FAIL") << "\n";

    if (!ok) {
        return 1;
    }

    std::cout << "\nV0.9.35 rover deployment + surface mobility campaign PASSED.\n";
    return 0;
}
