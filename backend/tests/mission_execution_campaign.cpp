#include "trishula/mission/mission_execution.h"

#include <iostream>
#include <stdexcept>

int main() {
    try {
        trishula::MissionExecutionConfiguration config{};
        config.step_seconds = 0.1;
        config.launch_duration_seconds = 3.0;
        config.earth_orbit_duration_seconds = 2.0;
        config.tli_duration_seconds = 2.0;
        config.lunar_cruise_duration_seconds = 4.0;
        config.lunar_orbit_duration_seconds = 2.0;
        config.descent_duration_seconds = 4.0;
        config.landing_duration_seconds = 2.0;
        config.rover_deployment_duration_seconds = 4.0;

        trishula::MissionExecutionEngine engine(config);
        const auto tli = engine.tli_plan();
        const auto loi = engine.loi_plan();
        if (!tli.valid || !loi.valid) throw std::runtime_error("mission maneuver planning failed");

        engine.run_until_complete(100000);
        const auto& snapshot = engine.snapshot();
        if (!engine.complete()) throw std::runtime_error("mission did not complete");
        if (!snapshot.landed || !snapshot.rover_deployed || !snapshot.rover_surface_ready ||
            !snapshot.rover_mission_complete) {
            throw std::runtime_error("mission did not traverse landing and rover operations");
        }
        if (snapshot.telemetry_sequence == 0) throw std::runtime_error("mission produced no telemetry ticks");

        std::cout << "TRISHULA V0.9.36 - End-to-End Mission Execution\n"
                  << "==============================================================\n"
                  << "  TLI plan valid                 : YES\n"
                  << "  LOI plan valid                 : YES\n"
                  << "  launch -> earth orbit          : ENABLED\n"
                  << "  earth orbit -> TLI             : ENABLED\n"
                  << "  TLI -> lunar cruise             : ENABLED\n"
                  << "  lunar cruise -> lunar orbit     : ENABLED\n"
                  << "  lunar orbit -> descent          : ENABLED\n"
                  << "  descent -> landing              : ENABLED\n"
                  << "  landing -> rover deployment    : ENABLED\n"
                  << "  rover -> surface operations     : ENABLED\n"
                  << "  telemetry ticks                 : " << snapshot.telemetry_sequence << "\n"
                  << "  final rover position            : " << snapshot.rover_x_m << ", " << snapshot.rover_y_m << " m\n"
                  << "  final mission phase             : " << trishula::MissionExecutionEngine::phase_name(snapshot.phase) << "\n"
                  << "  end-to-end mission execution    : PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.36 campaign FAILED: " << ex.what() << '\n';
        return 1;
    }
}
