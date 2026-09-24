#include <cmath>
#include <iostream>
#include <stdexcept>

#include "trishula/ground/command_execution.h"
#include "trishula/ground/vehicle_command_adapter.h"

namespace {
using namespace trishula;

GroundCommandRecord command(const char* id, const char* opcode, const char* parameters) {
    GroundCommandRecord value{};
    value.command_id = id;
    value.source_node = "MISSION_CONTROL";
    value.destination_node = "ROVER";
    value.target = "ROVER";
    value.opcode = opcode;
    value.parameters = parameters;
    value.application_id = 220U;
    value.mission_timestamp_ns = 987654321ULL;
    value.sequence_number = 1U;
    return value;
}

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_invalid_numeric_suffix_is_rejected_without_state_change() {
    GroundCommandExecutionEngine engine;
    VehicleCommandAdapter adapter;

    const auto start = command("CMD-ADP-SAFE-0001", "START_ROVER", "");
    require(adapter.apply(start, engine.execute(start)).status == VehicleAdapterStatus::Applied,
            "rover start failed");

    const auto before = adapter.snapshot();
    const auto invalid = command("CMD-ADP-SAFE-0002", "DRIVE", "x=25m|y=0|dt=0.1");
    const auto result = adapter.apply(invalid, engine.execute(invalid));

    require(result.status == VehicleAdapterStatus::InvalidParameters,
            "numeric suffix was incorrectly accepted");
    require(std::abs(result.snapshot.x_m - before.x_m) < 1e-12,
            "invalid command changed rover x position");
    require(std::abs(result.snapshot.y_m - before.y_m) < 1e-12,
            "invalid command changed rover y position");
    require(std::abs(result.snapshot.speed_m_s - before.speed_m_s) < 1e-12,
            "invalid command changed rover speed");
    require(result.snapshot.physical_steps == before.physical_steps,
            "invalid command advanced physical step count");
}

} // namespace

int main() {
    try {
        test_invalid_numeric_suffix_is_rejected_without_state_change();
        std::cout
            << "TRISHULA V0.9.54 - Vehicle Command Adapter Safety Boundary\\n"
            << "==============================================================\\n"
            << "  malformed numeric parameters rejected : PASS\\n"
            << "  invalid command leaves physical state unchanged : PASS\\n"
            << "  campaign : PASS\\n\\n"
            << "V0.9.54 vehicle command adapter safety campaign PASSED.\\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.54 safety campaign failed: " << error.what() << '\n';
        return 1;
    }
}
