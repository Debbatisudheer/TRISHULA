#include "trishula/mission/physical_mission_telemetry_loop.h"

#include <iostream>
#include <stdexcept>

namespace {
void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        trishula::PhysicalMissionTelemetryLoopConfiguration cfg{};
        cfg.mission.step_seconds = 1.0;
        cfg.mission.earth_orbit_hold_seconds = 2.0;
        cfg.mission.tli_burn_seconds = 2.0;
        cfg.mission.lunar_cruise_seconds = 2.0;
        cfg.mission.lunar_orbit_hold_seconds = 2.0;
        cfg.mission.descent_seconds = 2.0;
        cfg.mission.landing_seconds = 2.0;

        trishula::PhysicalMissionTelemetryLoop loop(cfg);
        loop.run(12U);

        const auto& stats = loop.stats();
        require(stats.simulation_ticks == 12U, "simulation tick count incorrect");
        require(stats.packets_encoded == 132U, "packet count incorrect");
        require(stats.packets_received == 132U, "ground receive count incorrect");
        require(stats.telemetry_records_accepted == 132U, "telemetry acceptance count incorrect");
        require(stats.rejected_packets == 0U, "telemetry loop rejected packets");
        require(loop.ground_station().stats().packets_accepted == 132U, "ground station acceptance mismatch");
        require(loop.telemetry().stats().packets_accepted == 132U, "telemetry pipeline acceptance mismatch");
        require(loop.mission().snapshot().telemetry_sequence == 12U, "mission telemetry sequence did not advance");
        require(loop.mission().snapshot().mission_time_seconds == 12.0, "mission time did not advance continuously");
        require(loop.mission().phase() == trishula::MissionPhase::Complete, "accelerated mission did not reach complete phase");

        trishula::TelemetryRecord current{};
        require(loop.telemetry().get_current("SPACECRAFT-01", "SPACECRAFT-01", "speed_m_per_s", current),
                "latest physical speed telemetry missing");
        require(current.sequence_number == 129U, "latest speed telemetry sequence mismatch");

        std::cout
            << "TRISHULA V0.9.39 - Continuous Physical Mission Telemetry Loop\n"
            << "==============================================================\n"
            << "  SimulationEngine continuous ticks : PASS\n"
            << "  physical telemetry metrics/tick   : 11\n"
            << "  space-link packet encoding        : PASS\n"
            << "  ground station reception          : PASS\n"
            << "  telemetry pipeline ingestion      : PASS\n"
            << "  simulation ticks                  : " << stats.simulation_ticks << "\n"
            << "  telemetry records accepted        : " << stats.telemetry_records_accepted << "\n"
            << "  final mission phase               : COMPLETE\n"
            << "  continuous physical telemetry     : PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.39 campaign FAILED: " << error.what() << '\n';
        return 1;
    }
}
