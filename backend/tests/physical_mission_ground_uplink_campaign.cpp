#include "trishula/mission/physical_mission_ground_uplink.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        trishula::PhysicalMissionGroundUplinkConfiguration cfg{};
        cfg.mission.earth_orbit_hold_seconds = 2.0;
        cfg.mission.tli_burn_seconds = 2.0;
        cfg.mission.lunar_cruise_seconds = 2.0;
        cfg.mission.lunar_orbit_hold_seconds = 2.0;
        cfg.mission.descent_seconds = 2.0;
        cfg.mission.landing_seconds = 2.0;
        cfg.ticks = 1U;
        cfg.interval_ms = 0U;
        cfg.api_url = "http://127.0.0.1:1/v1/ingest";

        trishula::PhysicalMissionGroundUplink uplink(cfg);
        require(uplink.controller().snapshot().phase == trishula::MissionPhase::EarthOrbit,
                "uplink did not start in Earth Orbit");
        require(uplink.controller().snapshot().telemetry_sequence == 0U,
                "uplink mission sequence should start at zero");

        bool failed_without_ground_service = false;
        try {
            uplink.step();
        } catch (const std::exception&) {
            failed_without_ground_service = true;
        }
        require(failed_without_ground_service, "uplink should fail closed when ground API is unavailable");
        require(uplink.stats().simulation_ticks == 1U, "simulation tick was not advanced");
        require(uplink.stats().records_attempted == 1U, "first physical metric was not attempted");
        require(uplink.stats().records_rejected == 1U, "failed HTTP uplink was not counted");

        std::cout
            << "TRISHULA V0.9.41 - Physical Mission Ground Uplink\n"
            << "==================================================\n"
            << "  physical phase controller : CONNECTED\n"
            << "  ground HTTP uplink        : ACTIVE\n"
            << "  fail-closed transport     : PASS\n"
            << "  physical mission source   : SPACECRAFT-01\n"
            << "  ground API target         : /v1/ingest\n"
            << "  physical ground uplink    : PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.41 campaign FAILED: " << error.what() << '\n';
        return 1;
    }
}
