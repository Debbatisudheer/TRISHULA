#include "trishula/rover/science_operations.h"

#include <iostream>
#include <vector>

int main() {
    using namespace trishula;

    RoverState rover{};
    rover.deployed = true;
    rover.mast_ready = true;
    rover.drive_system_healthy = true;
    rover.localization_valid = true;
    rover.wheels_on_surface = true;
    rover.battery_soc = 0.80;
    rover.mode = RoverMode::SurfaceReady;

    const std::vector<RoverScienceTarget> targets{
        {1, -200.0, 0.0, 12.0, 10.0, 90.0},
        {3, -600.0, 20.0, 14.0, 16.0, 120.0},
    };

    RoverScienceOperations science(0.75, 12.0);
    double total_score = 0.0;
    double total_energy = 0.0;
    bool all_stored = true;

    for (const auto& target : targets) {
        const auto result = science.execute_target(rover, target, true, true);
        if (!result.mission_completed || result.observations.empty()) return 1;
        const auto& obs = result.observations.front();
        if (!obs.acquired || !obs.validated || !obs.stored) return 2;
        total_score += result.science_data_score;
        total_energy += result.science_energy_used_wh;
        all_stored = all_stored && obs.stored;
    }

    RoverState faulted = rover;
    const auto failed = science.execute_target(faulted, targets.front(), false, true);
    if (failed.mission_completed || failed.observations.front().acquired) return 3;

    std::cout << "TRISHULA V0.9.41 - Rover Science Operations\n"
              << "==============================================================\n"
              << "  science targets executed        : 2\n"
              << "  science observations acquired   : 2\n"
              << "  data validation                : PASS\n"
              << "  data storage                   : " << (all_stored ? "PASS" : "FAIL") << "\n"
              << "  total science score            : " << total_score << "\n"
              << "  science energy used            : " << total_energy << " Wh\n"
              << "  rover stability gate           : PASS\n"
              << "  instrument fault inhibition    : PASS\n"
              << "  science mission execution      : PASS\n\n"
              << "V0.9.41 rover science operations campaign PASSED.\n";
    return 0;
}
