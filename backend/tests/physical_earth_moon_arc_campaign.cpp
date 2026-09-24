#include "trishula/mission/physical_mission_execution.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        trishula::PhysicalMissionExecutionConfiguration cfg{};
        cfg.step_seconds = 1.0;
        cfg.earth_orbit_hold_seconds = 10.0;
        cfg.event_driven_physical_arc = true;
        trishula::PhysicalMissionExecutionEngine mission(cfg);
        mission.run_until_phase(trishula::MissionPhase::LunarOrbit, 1000000U);
        const auto& s = mission.snapshot();
        const double earth_distance = std::sqrt(s.position_x_m*s.position_x_m + s.position_y_m*s.position_y_m + s.position_z_m*s.position_z_m);
        if (s.phase != trishula::MissionPhase::LunarOrbit) throw std::runtime_error("did not reach lunar orbit phase");
        if (s.telemetry_sequence == 0U) throw std::runtime_error("telemetry did not advance");
        if (s.distance_to_moon_m > 70000000.0) throw std::runtime_error("not inside lunar sphere of influence");
        if (earth_distance < 300000000.0) throw std::runtime_error("spacecraft did not leave Earth vicinity");
        std::cout << std::fixed << std::setprecision(3)
                  << "TRISHULA V0.9.41.1 - Physical Earth-Moon Arc\n"
                  << "==============================================================\n"
                  << "  event-driven physical arc     : ENABLED\n"
                  << "  TLI finite burn               : EXECUTED\n"
                  << "  Earth-Moon coast              : PHYSICAL\n"
                  << "  lunar SOI arrival             : DETECTED\n"
                  << "  mission time                  : " << s.mission_time_seconds << " s\n"
                  << "  distance to Moon              : " << s.distance_to_moon_m << " m\n"
                  << "  Earth-centered distance       : " << earth_distance << " m\n"
                  << "  telemetry sequence            : " << s.telemetry_sequence << "\n"
                  << "  final physical phase          : Lunar Orbit\n"
                  << "  physical Earth-Moon arc       : PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.41.1 campaign FAILED: " << ex.what() << '\n';
        return 1;
    }
}
