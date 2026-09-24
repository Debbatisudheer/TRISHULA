#include <iostream>
#include <stdexcept>
#include <string>

#include "trishula/ground/command_execution.h"

namespace {
using namespace trishula;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

GroundCommandRecord command(const char* id, const char* target, const char* opcode, const char* parameters = "") {
    GroundCommandRecord value{};
    value.command_id = id;
    value.source_node = "MISSION_CONTROL";
    value.destination_node = "VIKRAM";
    value.target = target;
    value.opcode = opcode;
    value.parameters = parameters;
    value.application_id = 210U;
    value.mission_timestamp_ns = 123456789ULL;
    value.sequence_number = 1U;
    return value;
}

void test_rover_command_changes_state_and_generates_downlink() {
    GroundCommandExecutionEngine engine;
    const auto result = engine.execute(command("CMD-EX-0001", "ROVER", "START_ROVER"));
    require(result.status == CommandExecutionStatus::Executed, "START_ROVER was not executed");
    require(result.state.mode == "DRIVING", "rover mode did not change to DRIVING");
    require(result.state.commands_executed == 1U, "executed command count incorrect");
    require(result.event_packet.header.type == GroundPacketType::Event, "execution event is not an event packet");
    require(result.telemetry_packet.header.type == GroundPacketType::Telemetry, "execution telemetry is not telemetry");
}

void test_safe_mode_blocks_motion_and_reset_recovers() {
    GroundCommandExecutionEngine engine;
    const auto safe = engine.execute(command("CMD-EX-0002", "ROVER", "ENTER_SAFE_MODE"));
    require(safe.status == CommandExecutionStatus::Executed, "ENTER_SAFE_MODE failed");
    require(safe.state.safe_mode, "safe mode was not asserted");
    require(safe.state.mode == "SAFE", "safe mode state not reflected");

    const auto blocked = engine.execute(command("CMD-EX-0003", "ROVER", "DRIVE", "x=10|y=4"));
    require(blocked.status == CommandExecutionStatus::PreconditionsFailed, "DRIVE escaped safe mode");

    const auto reset = engine.execute(command("CMD-EX-0004", "ROVER", "RESET_SUBSYSTEM", "mobility"));
    require(reset.status == CommandExecutionStatus::Executed, "RESET_SUBSYSTEM failed");
    require(reset.state.subsystem_reset_count == 1U, "reset count incorrect");

    const auto exit_safe = engine.execute(command("CMD-EX-0005", "ROVER", "EXIT_SAFE_MODE"));
    require(exit_safe.status == CommandExecutionStatus::Executed, "EXIT_SAFE_MODE failed");
    require(!exit_safe.state.safe_mode, "safe mode did not clear");
    require(exit_safe.state.mode == "STANDBY", "safe-mode recovery state incorrect");
}

void test_command_execution_feedback_roundtrip() {
    GroundCommandExecutionEngine engine;
    const auto result = engine.execute(command("CMD-EX-0006", "VIKRAM", "TRANSMIT_DATA", "science"));
    require(result.status == CommandExecutionStatus::Executed, "TRANSMIT_DATA failed");
    require(result.state.transmit_requested, "transmit request not reflected in state");
    const std::string event_payload(result.event_packet.payload.begin(), result.event_packet.payload.end());
    const std::string telemetry_payload(result.telemetry_packet.payload.begin(), result.telemetry_packet.payload.end());
    require(event_payload.find("command_id=CMD-EX-0006") != std::string::npos, "event feedback lost command id");
    require(telemetry_payload.find("transmit_requested=true") != std::string::npos, "telemetry feedback lost state");
}

void test_invalid_target() {
    GroundCommandExecutionEngine engine;
    const auto result = engine.execute(command("CMD-EX-0007", "UNKNOWN", "STOP"));
    require(result.status == CommandExecutionStatus::TargetUnavailable, "unknown target was executed");
}

} // namespace

int main() {
    try {
        test_rover_command_changes_state_and_generates_downlink();
        test_safe_mode_blocks_motion_and_reset_recovers();
        test_command_execution_feedback_roundtrip();
        test_invalid_target();
        std::cout
            << "TRISHULA V0.9.53 - Ground Command Execution Loop\n"
            << "=================================================\n"
            << "  command changes vehicle state        : PASS\n"
            << "  command execution generates event    : PASS\n"
            << "  command execution generates telemetry: PASS\n"
            << "  safe-mode enforcement/recovery       : PASS\n"
            << "  target validation                    : PASS\n"
            << "  campaign                             : PASS\n\n"
            << "V0.9.53 ground command execution campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.53 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
