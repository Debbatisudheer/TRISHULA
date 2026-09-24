#include <iostream>
#include <vector>

#include "trishula/rover/science_driven_mission_planner.h"

int main() {
    using namespace trishula;

    LunarTerrainMap terrain;
    AutonomousRoverNavigator navigator(2.0, 2.5);
    RoverScienceOperations science(0.75, 12.0);
    RoverState rover{};
    rover.deployed = true;
    rover.mast_ready = true;
    rover.drive_system_healthy = true;
    rover.localization_valid = true;
    rover.wheels_on_surface = true;
    rover.battery_soc = 0.85;
    rover.mode = RoverMode::SurfaceReady;

    const RoverScienceTarget first{1, -200.0, 0.0, 10.0, 12.0, 12.0};
    const RoverScienceTarget discovery_target{3, 0.0, 20.0, 16.0, 20.0, 14.0};
    const RoverScienceTarget ordinary_target{4, -250.0, -40.0, 10.0, 10.0, 13.0};

    const auto first_obs = science.execute_target(rover, first, true, true).observations.front();
    if (!first_obs.stored) { std::cerr << "first observation not stored, q=" << first_obs.quality << "\n"; return 1; }

    RoverScienceDrivenMissionPlanner mission_planner;
    RoverScienceDrivenMissionState state{};
    state.current_position = RoverMissionTarget{first.x_m, first.y_m};
    state.available_energy_wh = 820.0;
    state.reserve_energy_wh = 180.0;
    state.remaining_targets = {discovery_target, ordinary_target};
    state.previous_target_id = first.id;

    const auto result = mission_planner.replan_after_observation(
        terrain, state, navigator, 800.0,
        RoverScienceObservation{first.id, 0.92, 12.0, true, true, true, true},
        discovery_target.id, 95.0);

    if (!result.feasible) { std::cerr << "first infeasible\n"; return 2; }
    if (result.selected_target_id != discovery_target.id) { std::cerr << "selected=" << result.selected_target_id << "\n"; return 3; }
    if (!result.newly_discovered_priority || !result.mission_priority_changed) return 4;
    if (result.discovery_bonus != 95.0) return 5;
    if (result.science_quality < 0.9) return 6;
    if (result.planner_result.energy_after_route_wh <= state.reserve_energy_wh) return 7;

    const auto no_discovery = mission_planner.replan_after_observation(
        terrain, state, navigator, 800.0,
        RoverScienceObservation{first.id, 0.20, 12.0, true, true, false, false},
        discovery_target.id, 95.0);
    if (!no_discovery.feasible || no_discovery.newly_discovered_priority || no_discovery.discovery_bonus != 0.0) return 8;

    std::cout << "TRISHULA V0.9.42 - Science-Driven Mission Replanning\n"
              << "==============================================================\n"
              << "  initial science target             : " << first.id << "\n"
              << "  observation quality               : 0.920000\n"
              << "  validated science result           : YES\n"
              << "  new discovery target               : " << discovery_target.id << "\n"
              << "  discovery bonus                    : " << result.discovery_bonus << "\n"
              << "  selected next target               : " << result.selected_target_id << "\n"
              << "  mission priority changed           : " << (result.mission_priority_changed ? "YES" : "NO") << "\n"
              << "  remaining energy after route       : " << result.planner_result.energy_after_route_wh << " Wh\n"
              << "  science-driven replanning          : PASS\n"
              << "  low-quality discovery ignored      : PASS\n"
              << "  energy reserve respected            : PASS\n\n"
              << "V0.9.42 science-driven mission replanning campaign PASSED.\n";
    return 0;
}
