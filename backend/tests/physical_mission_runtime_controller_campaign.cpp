#include "trishula/mission/physical_mission_runtime_controller.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using State = trishula::PhysicalMissionRuntimeState;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        trishula::PhysicalMissionExecutionConfiguration cfg{};
        cfg.step_seconds = 1.0;
        cfg.earth_orbit_hold_seconds = 10.0;
        cfg.tli_burn_seconds = 10.0;
        cfg.event_driven_physical_arc = false;

        trishula::PhysicalMissionRuntimeController controller(cfg);
        require(controller.snapshot().state == State::Ready, "initial state is not Ready");

        const auto before_start = controller.snapshot().physical;
        controller.start();
        require(controller.snapshot().state == State::Running, "start did not enter Running");

        require(controller.step(), "running step did not execute");
        const auto after_step = controller.snapshot().physical;
        require(after_step.telemetry_sequence > before_start.telemetry_sequence, "physical telemetry did not advance");
        require(after_step.mission_time_seconds > before_start.mission_time_seconds, "physical mission time did not advance");
        require(after_step.power_status == "SUPPLIED", "power status is not supplied");
        require(after_step.propulsion_status == "COASTING", "propulsion status is not coasting");
        require(after_step.thermal_status == "NOMINAL", "thermal status is not nominal");
        require(after_step.communication_status == "LOCKED", "communication status is not locked");
        require(after_step.navigation_status == "NOMINAL", "navigation status is not nominal");
        require(after_step.health_status == "NOMINAL", "health status is not nominal");
        require(after_step.battery_soc < 1.0 && after_step.battery_soc > 0.0, "power model did not evolve");

        controller.pause();
        require(controller.snapshot().state == State::Paused, "pause did not enter Paused");
        const auto paused = controller.snapshot().physical;
        require(!controller.step(), "paused step unexpectedly advanced");
        const auto paused_after = controller.snapshot().physical;
        require(paused_after.telemetry_sequence == paused.telemetry_sequence, "paused telemetry sequence changed");
        require(std::abs(paused_after.mission_time_seconds - paused.mission_time_seconds) < 1.0e-12,
                "paused mission time changed");
        require(std::abs(paused_after.position_x_m - paused.position_x_m) < 1.0e-9,
                "paused physical position changed");

        controller.resume();
        require(controller.snapshot().state == State::Running, "resume did not return to Running");
        require(controller.step(), "resumed step did not execute");
        require(controller.snapshot().physical.telemetry_sequence > paused.telemetry_sequence,
                "resume did not advance physical telemetry");

        controller.abort();
        require(controller.snapshot().state == State::Aborted, "abort did not enter Aborted");
        const auto aborted = controller.snapshot().physical;
        require(!controller.step(), "aborted step unexpectedly advanced");
        require(controller.snapshot().physical.telemetry_sequence == aborted.telemetry_sequence,
                "aborted physical telemetry changed");

        std::cout
            << "TRISHULA V0.9.113.1 - Spacecraft Subsystem Status\n"
            << "==============================================================\n"
            << "  start -> running               : PASS\n"
            << "  physical step                  : PASS\n"
            << "  pause freezes physical state   : PASS\n"
            << "  resume continues same state    : PASS\n"
            << "  abort stops physical stepping  : PASS\n"
            << "  no UI-only state progression   : PASS\n"
            << "  power subsystem status         : PASS\n"
            << "  propulsion subsystem status    : PASS\n"
            << "  thermal subsystem status       : PASS\n"
            << "  communication/navigation       : PASS\n"
            << "  spacecraft health aggregation : PASS\n"
            << "  subsystem status foundation    : PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.112.1 campaign FAILED: " << ex.what() << '\n';
        return 1;
    }
}
