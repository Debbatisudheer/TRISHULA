#include "trishula/mission/physical_mission_execution.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        trishula::PhysicalMissionExecutionConfiguration cfg{};
        cfg.step_seconds = 1.0;
        cfg.earth_orbit_hold_seconds = 5.0;
        cfg.tli_burn_seconds = 5.0;
        cfg.lunar_cruise_seconds = 5.0;
        cfg.lunar_orbit_hold_seconds = 5.0;
        cfg.descent_seconds = 5.0;
        cfg.landing_seconds = 5.0;

        trishula::PhysicalMissionExecutionEngine engine(cfg);
        const auto initial = engine.snapshot();
        if (initial.phase != trishula::MissionPhase::EarthOrbit) throw std::runtime_error("wrong initial phase");
        if (!(initial.speed_m_per_s > 7000.0 && initial.speed_m_per_s < 9000.0)) throw std::runtime_error("parking orbit state invalid");

        for (int i = 0; i < 5; ++i) engine.step();
        if (engine.phase() != trishula::MissionPhase::TransLunarInjection) throw std::runtime_error("TLI transition missing");

        const auto before_burn = engine.snapshot();
        for (int i = 0; i < 5; ++i) engine.step();
        const auto after_burn = engine.snapshot();
        if (!(after_burn.speed_m_per_s > before_burn.speed_m_per_s)) throw std::runtime_error("physical TLI burn did not increase velocity");
        if (after_burn.telemetry_sequence <= before_burn.telemetry_sequence) throw std::runtime_error("telemetry sequence did not advance");
        if (!std::isfinite(after_burn.distance_to_moon_m)) throw std::runtime_error("moon-relative state invalid");

        std::cout << "TRISHULA V0.9.37 - Physical Mission Execution Bridge\n"
                  << "==============================================================\n"
                  << "  real SimulationEngine state    : CONNECTED\n"
                  << "  Earth parking orbit state      : VALID\n"
                  << "  TLI physical propagation       : ACTIVE\n"
                  << "  Moon ephemeris                 : ACTIVE\n"
                  << "  telemetry sequence             : " << after_burn.telemetry_sequence << "\n"
                  << "  post-TLI speed                 : " << after_burn.speed_m_per_s << " m/s\n"
                  << "  physical mission bridge        : PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.37 campaign FAILED: " << ex.what() << '\n';
        return 1;
    }
}
