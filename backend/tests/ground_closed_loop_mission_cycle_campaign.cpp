#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "trishula/ground/closed_loop_mission_cycle.h"

namespace {
using namespace trishula;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

GroundCommandRecord command(const char* id, const std::uint32_t sequence,
                            const char* opcode, const char* parameters = "") {
    GroundCommandRecord value{};
    value.command_id = id;
    value.source_node = "MISSION_CONTROL";
    value.destination_node = "GROUND";
    value.target = "ROVER";
    value.opcode = opcode;
    value.parameters = parameters;
    value.priority = GroundPacketPriority::High;
    value.application_id = 301U;
    value.mission_timestamp_ns = 100000000ULL + sequence;
    value.sequence_number = sequence;
    return value;
}

void test_ordered_cycle_executes_and_returns_feedback() {
    GroundClosedLoopMissionCycle cycle;
    const std::vector<GroundCommandRecord> commands{
        command("CMD-CYCLE-0001", 1U, "START_ROVER"),
        command("CMD-CYCLE-0002", 2U, "DRIVE", "x=5|y=2|dt=0.1"),
        command("CMD-CYCLE-0003", 3U, "STOP_ROVER")
    };

    const auto result = cycle.process(commands);
    require(result.status == ClosedLoopMissionCycleStatus::Applied, "ordered cycle did not apply");
    require(result.commands_processed == 3U, "not all cycle commands were processed");
    require(result.events_ingested == 3U, "event feedback count does not match cycle");
    require(result.telemetry_records_ingested == 18U, "telemetry feedback count does not match cycle");
    require(result.physical_steps_after > result.physical_steps_before,
            "cycle did not produce a physical rover state change");

    TelemetryRecord speed{};
    require(cycle.feedback_engine().telemetry_pipeline().get_current(
                "ROVER", "ROVER", "rover_speed_m_s", speed),
            "cycle did not return current rover telemetry");
    require(speed.value == 0.0, "final telemetry does not reflect STOP_ROVER physical state");
}

void test_sequence_violation_prevents_any_physical_progress() {
    GroundClosedLoopMissionCycle cycle;
    const std::vector<GroundCommandRecord> commands{
        command("CMD-CYCLE-0004", 10U, "START_ROVER"),
        command("CMD-CYCLE-0005", 10U, "DRIVE", "x=1|y=0|dt=0.1")
    };

    const auto result = cycle.process(commands);
    require(result.status == ClosedLoopMissionCycleStatus::SequenceViolation,
            "sequence violation was not rejected");
    require(result.commands_processed == 0U, "invalid cycle partially executed");
    require(cycle.feedback_engine().vehicle_adapter().snapshot().physical_steps == 0U,
            "sequence violation changed physical state");
}

void test_failed_command_stops_cycle_and_does_not_claim_success() {
    GroundClosedLoopMissionCycle cycle;
    const std::vector<GroundCommandRecord> commands{
        command("CMD-CYCLE-0006", 20U, "START_ROVER"),
        command("CMD-CYCLE-0007", 21U, "DRIVE", "x=bad|y=0|dt=0.1"),
        command("CMD-CYCLE-0008", 22U, "DRIVE", "x=10|y=0|dt=0.1")
    };

    const auto result = cycle.process(commands);
    require(result.status == ClosedLoopMissionCycleStatus::CommandFailed,
            "failed command did not stop cycle");
    require(result.commands_processed == 1U, "cycle continued after failed command");
    require(result.physical_steps_after == result.physical_steps_before,
            "failed command advanced physical state");
    require(result.events_ingested == 2U && result.telemetry_records_ingested == 12U,
            "ground did not receive real execution/failure feedback");
}

} // namespace

int main() {
    try {
        test_ordered_cycle_executes_and_returns_feedback();
        test_sequence_violation_prevents_any_physical_progress();
        test_failed_command_stops_cycle_and_does_not_claim_success();
        std::cout
            << "TRISHULA V0.9.55 - Closed-Loop Mission Cycle\n"
            << "===============================================================\n"
            << "  ordered commands execute with physical feedback : PASS\n"
            << "  sequence violation causes zero physical progress : PASS\n"
            << "  failed command stops cycle without fake feedback : PASS\n"
            << "  campaign                                          : PASS\n\n"
            << "V0.9.55 closed-loop mission cycle campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.55 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
