#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "trishula/ground/closed_loop_feedback.h"

namespace {
using namespace trishula;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

GroundCommandRecord command(const char* id, const char* opcode, const char* parameters) {
    GroundCommandRecord value{};
    value.command_id = id;
    value.source_node = "MISSION_CONTROL";
    value.destination_node = "GROUND";
    value.target = "ROVER";
    value.opcode = opcode;
    value.parameters = parameters;
    value.priority = GroundPacketPriority::High;
    value.application_id = 301U;
    value.mission_timestamp_ns = 123456789ULL;
    value.sequence_number = 1U;
    return value;
}

void test_drive_command_returns_feedback() {
    GroundClosedLoopFeedbackEngine engine;
    const auto start = engine.process(command("CMD-CL-0001", "START_ROVER", ""));
    require(start.status == ClosedLoopStatus::Applied, "START_ROVER closed loop failed");
    require(start.events_ingested == 1U, "START_ROVER event feedback missing");
    require(start.telemetry_records_ingested == 6U, "START_ROVER telemetry feedback count incorrect");

    const auto drive = engine.process(command("CMD-CL-0002", "DRIVE", "x=5|y=2|dt=0.1"));
    require(drive.status == ClosedLoopStatus::Applied, "DRIVE closed loop failed");
    require(drive.physical.rover_metrics.distance_to_target_m > 0.0, "rover did not evaluate physical target");
    require(drive.events_ingested == 1U, "DRIVE event feedback missing");
    require(drive.telemetry_records_ingested == 6U, "DRIVE telemetry feedback count incorrect");

    TelemetryRecord current{};
    require(engine.telemetry_pipeline().get_current("ROVER", "ROVER", "rover_x_m", current), "rover x telemetry missing");
    require(current.value > 0.0, "rover x telemetry did not reflect physical state");
    require(engine.event_pipeline().archived_events() == 2U, "event feedback archive size incorrect");
}

void test_feedback_contains_physical_state_metrics() {
    GroundClosedLoopFeedbackEngine engine;
    require(engine.process(command("CMD-CL-0003", "START_ROVER", "")).status == ClosedLoopStatus::Applied,
            "start command failed");
    const auto drive = engine.process(command("CMD-CL-0004", "DRIVE", "x=1|y=1|dt=0.2"));
    require(drive.status == ClosedLoopStatus::Applied, "drive command failed");

    TelemetryRecord x{};
    TelemetryRecord speed{};
    TelemetryRecord battery{};
    require(engine.telemetry_pipeline().get_current("ROVER", "ROVER", "rover_x_m", x), "x metric missing");
    require(engine.telemetry_pipeline().get_current("ROVER", "ROVER", "rover_speed_m_s", speed), "speed metric missing");
    require(engine.telemetry_pipeline().get_current("ROVER", "ROVER", "rover_battery_soc", battery), "battery metric missing");
    require(std::isfinite(x.value) && std::isfinite(speed.value) && std::isfinite(battery.value),
            "feedback contained non-finite physical state");
}

void test_logical_failure_does_not_claim_closed_loop_success() {
    GroundClosedLoopFeedbackEngine engine;
    const auto bad = command("CMD-CL-0005", "UNSUPPORTED_OPCODE", "");
    const auto result = engine.process(bad);
    require(result.status == ClosedLoopStatus::LogicalExecutionFailed,
            "logical failure incorrectly claimed closed-loop success");
    require(result.telemetry_records_ingested == 0U && result.events_ingested == 0U,
            "logical failure produced false physical feedback");
}

} // namespace

int main() {
    try {
        test_drive_command_returns_feedback();
        test_feedback_contains_physical_state_metrics();
        test_logical_failure_does_not_claim_closed_loop_success();
        std::cout
            << "TRISHULA V0.9.55 - Vehicle Response & Closed-Loop Ground Feedback\n"
            << "====================================================================\n"
            << "  command drives physical rover and returns feedback : PASS\n"
            << "  physical state telemetry projected to ground       : PASS\n"
            << "  logical failure does not fake physical feedback    : PASS\n"
            << "  campaign                                            : PASS\n\n"
            << "V0.9.55 closed-loop feedback campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.55 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
