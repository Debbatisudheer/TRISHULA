#include "trishula/mission/physical_mission_phase_controller.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        trishula::PhysicalMissionExecutionConfiguration cfg{};
        cfg.step_seconds = 1.0;
        cfg.earth_orbit_hold_seconds = 2.0;
        cfg.tli_burn_seconds = 2.0;
        cfg.lunar_cruise_seconds = 2.0;
        cfg.lunar_orbit_hold_seconds = 2.0;
        cfg.descent_seconds = 2.0;
        cfg.landing_seconds = 2.0;

        trishula::PhysicalMissionPhaseController controller(cfg);
        require(controller.snapshot().phase == trishula::MissionPhase::EarthOrbit, "initial phase invalid");
        require(controller.snapshot().phase_progress == 0.0, "initial phase progress invalid");

        controller.run(20U);
        const auto& s = controller.snapshot();
        require(s.complete, "mission did not complete");
        require(s.phase == trishula::MissionPhase::Complete, "final phase invalid");
        require(s.telemetry_sequence == 12U, "telemetry sequence mismatch");
        require(s.phase_transition_count == 6U, "phase transition count mismatch");
        require(controller.transitions().size() == 6U, "transition history size mismatch");
        require(controller.transitions()[0].from == trishula::MissionPhase::EarthOrbit,
                "first transition source mismatch");
        require(controller.transitions()[0].to == trishula::MissionPhase::TransLunarInjection,
                "first transition target mismatch");
        require(controller.transitions().back().to == trishula::MissionPhase::Complete,
                "last transition target mismatch");
        require(std::isfinite(controller.mission().snapshot().distance_to_moon_m), "moon distance invalid");
        require(controller.mission().snapshot().speed_m_per_s > 0.0, "physical speed invalid");

        std::cout
            << "TRISHULA V0.9.40 - Continuous Physical Mission Phase Controller\n"
            << "==============================================================\n"
            << "  physical mission engine       : CONNECTED\n"
            << "  phase progress model          : ACTIVE\n"
            << "  physical telemetry sequence   : " << s.telemetry_sequence << "\n"
            << "  phase transitions recorded    : " << s.phase_transition_count << "\n"
            << "  final mission phase            : COMPLETE\n"
            << "  continuous phase execution     : PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.40 campaign FAILED: " << ex.what() << '\n';
        return 1;
    }
}
