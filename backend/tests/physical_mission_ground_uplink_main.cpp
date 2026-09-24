#include "trishula/mission/physical_mission_ground_uplink.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
std::string argument(int argc, char** argv, const char* name, std::string fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == name) return argv[i + 1];
    }
    return fallback;
}

std::size_t size_argument(int argc, char** argv, const char* name, std::size_t fallback) {
    const auto value = argument(argc, argv, name, "");
    if (value.empty()) return fallback;
    return static_cast<std::size_t>(std::stoull(value));
}

std::uint32_t uint_argument(int argc, char** argv, const char* name, std::uint32_t fallback) {
    const auto value = argument(argc, argv, name, "");
    if (value.empty()) return fallback;
    return static_cast<std::uint32_t>(std::stoul(value));
}

double double_argument(int argc, char** argv, const char* name, double fallback) {
    const auto value = argument(argc, argv, name, "");
    if (value.empty()) return fallback;
    return std::stod(value);
}
}

int main(int argc, char** argv) {
    try {
        trishula::PhysicalMissionGroundUplinkConfiguration cfg{};
        cfg.api_url = argument(argc, argv, "--api", cfg.api_url);
        cfg.mission_id = argument(argc, argv, "--mission", cfg.mission_id);
        cfg.source_node = argument(argc, argv, "--source", cfg.source_node);
        cfg.origin_node = argument(argc, argv, "--origin", cfg.origin_node);
        cfg.destination_node = argument(argc, argv, "--destination", cfg.destination_node);
        cfg.ticks = size_argument(argc, argv, "--ticks", cfg.ticks);
        cfg.interval_ms = uint_argument(argc, argv, "--interval-ms", cfg.interval_ms);
        cfg.mission.step_seconds = double_argument(argc, argv, "--step-seconds", 1.0);
        cfg.mission.event_driven_physical_arc = true;
        cfg.mission.earth_orbit_hold_seconds = double_argument(argc, argv, "--earth-orbit-seconds", 5.0);
        cfg.mission.tli_burn_seconds = double_argument(argc, argv, "--tli-seconds", 5.0);
        cfg.mission.lunar_cruise_seconds = double_argument(argc, argv, "--cruise-seconds", 5.0);
        cfg.mission.lunar_orbit_hold_seconds = double_argument(argc, argv, "--lunar-orbit-seconds", 5.0);
        cfg.mission.descent_seconds = double_argument(argc, argv, "--descent-seconds", 5.0);
        cfg.mission.landing_seconds = double_argument(argc, argv, "--landing-seconds", 5.0);

        trishula::PhysicalMissionGroundUplink uplink(cfg);
        uplink.run();
        const auto& stats = uplink.stats();
        const auto& snapshot = uplink.controller().snapshot();

        std::cout
            << "TRISHULA V0.9.41 - Physical Mission Ground Uplink\n"
            << "==================================================\n"
            << "  physical mission engine : CONNECTED\n"
            << "  ground API uplink       : ACTIVE\n"
            << "  simulation ticks        : " << stats.simulation_ticks << "\n"
            << "  telemetry records       : " << stats.records_accepted << "\n"
            << "  phase transitions       : " << stats.phase_transitions << "\n"
            << "  final mission phase     : " << static_cast<int>(snapshot.phase) << "\n"
            << "  physical ground uplink  : PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.41 uplink FAILED: " << error.what() << '\n';
        return 1;
    }
}
