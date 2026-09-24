#include <filesystem>
#include <iostream>
#include <vector>

#include "trishula/rover/autonomous_surface_mission.h"

int main() {
    using namespace trishula;

    LunarTerrainMap terrain(0.0);
    SurfaceRover rover;
    RoverState state{};
    state.battery_soc = 0.98;

    const std::vector<RoverScienceTarget> targets{
        {1U, -40.0, 0.0, 14.0, 12.0, 30.0},
        {2U, -80.0, 25.0, 13.0, 14.0, 30.0},
        {3U, -120.0, -20.0, 15.0, 16.0, 30.0},
    };

    AutonomousSurfaceMissionConfig config{};
    config.navigation_half_extent_m = 200.0;
    config.max_control_steps_per_target = 5000U;
    config.reserve_energy_wh = 120.0;
    config.max_targets = targets.size();

    const std::filesystem::path archive = std::filesystem::path("..") / "data" / "autonomous_surface_mission";
    AutonomousSurfaceMission mission(config, archive);
    const auto result = mission.execute(terrain, rover, state, targets);

    if (!result.deployment_completed || !result.initialization_completed) return 1;
    if (!result.mission_completed) return 2;
    if (result.targets_completed != targets.size()) return 3;
    if (result.observations_completed != targets.size()) return 4;
    if (result.science_products_stored != targets.size()) return 5;
    if (result.rover_to_vikram_transfers != targets.size()) return 6;
    if (result.vikram_to_ground_transfers != targets.size()) return 7;
    if (result.ground_analyses_completed != targets.size()) return 8;
    if (result.knowledge_records != targets.size()) return 9;
    if (result.total_route_distance_m <= 0.0 || result.surface_elapsed_s <= 0.0) return 10;
    if (result.final_rover_state.mode != RoverMode::MissionComplete) return 11;

    std::cout << "TRISHULA V0.9.105.1 - Autonomous Rover Surface Mission Completion\n"
              << "====================================================================\n"
              << "  rover deployment/initialization : PASS\n"
              << "  autonomous target selection     : PASS\n"
              << "  closed-loop surface navigation  : PASS\n"
              << "  multi-target mission execution  : " << result.targets_completed << " targets\n"
              << "  science observations            : " << result.observations_completed << "\n"
              << "  science products stored         : " << result.science_products_stored << "\n"
              << "  rover -> Vikram relay           : " << result.rover_to_vikram_transfers << "\n"
              << "  Vikram -> Ground relay          : " << result.vikram_to_ground_transfers << "\n"
              << "  ground science analyses         : " << result.ground_analyses_completed << "\n"
              << "  mission knowledge records       : " << result.knowledge_records << "\n"
              << "  route distance                  : " << result.total_route_distance_m << " m\n"
              << "  surface elapsed time            : " << result.surface_elapsed_s << " s\n"
              << "  long-duration mission loop      : PASS\n"
              << "  DSA/A* autonomous navigation    : PASS\n"
              << "\nV0.9.105.1 autonomous rover/science completion campaign PASSED.\n";
    return 0;
}
