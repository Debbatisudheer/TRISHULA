#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "trishula/ground/command_execution.h"
#include "trishula/ground/vehicle_command_adapter.h"

namespace {
using namespace trishula;

GroundCommandRecord command(const char* id, const char* target, const char* opcode, const char* parameters = "") {
    GroundCommandRecord value{};
    value.command_id = id;
    value.source_node = "MISSION_CONTROL";
    value.destination_node = target;
    value.target = target;
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

void test_start_rover_hits_real_surface_rover_interface() {
    GroundCommandExecutionEngine engine;
    VehicleCommandAdapter adapter;

    const auto start = command("CMD-ADP-0001", "ROVER", "START_ROVER");
    const auto logical = engine.execute(start);
    const auto physical = adapter.apply(start, logical);

    require(physical.status == VehicleAdapterStatus::Applied, "START_ROVER was not applied physically");
    require(physical.snapshot.deployed, "rover was not physically deployed");
    require(physical.snapshot.mast_ready, "rover surface systems were not initialized");
    require(physical.snapshot.mode == "SURFACE_READY", "physical rover is not surface-ready");
}

void test_drive_advances_physical_state() {
    GroundCommandExecutionEngine engine;
    VehicleCommandAdapter adapter;

    const auto start = command("CMD-ADP-0002", "ROVER", "START_ROVER");
    const auto start_result = engine.execute(start);
    require(adapter.apply(start, start_result).status == VehicleAdapterStatus::Applied,
            "rover start failed");

    const auto before = adapter.snapshot();
    const auto drive = command("CMD-ADP-0003", "ROVER", "DRIVE", "x=25|y=0|dt=0.1");
    const auto logical = engine.execute(drive);
    const auto physical = adapter.apply(drive, logical);

    require(physical.status == VehicleAdapterStatus::Applied, "DRIVE was not physically applied");
    require(physical.snapshot.physical_steps == 1U, "physical step count did not increment");
    require(std::hypot(physical.snapshot.x_m - before.x_m, physical.snapshot.y_m - before.y_m) > 0.0,
            "rover physical position did not change");
    require(physical.snapshot.speed_m_s > 0.0, "rover physical speed did not increase");
}

void test_stop_affects_real_rover_state() {
    GroundCommandExecutionEngine engine;
    VehicleCommandAdapter adapter;

    const auto start = command("CMD-ADP-0004", "ROVER", "START_ROVER");
    require(adapter.apply(start, engine.execute(start)).status == VehicleAdapterStatus::Applied,
            "rover start failed");

    const auto drive = command("CMD-ADP-0005", "ROVER", "DRIVE", "x=50|y=0|dt=0.1");
    require(adapter.apply(drive, engine.execute(drive)).status == VehicleAdapterStatus::Applied,
            "rover drive failed");
    require(adapter.snapshot().speed_m_s > 0.0, "rover is not moving before STOP_ROVER");

    const auto stop = command("CMD-ADP-0006", "ROVER", "STOP_ROVER");
    const auto physical = adapter.apply(stop, engine.execute(stop));
    require(physical.status == VehicleAdapterStatus::Applied, "STOP_ROVER was not applied physically");
    require(std::abs(physical.snapshot.speed_m_s) < 1e-12, "physical rover speed did not stop");
}

void test_drive_requires_numeric_coordinates() {
    GroundCommandExecutionEngine engine;
    VehicleCommandAdapter adapter;
    const auto start = command("CMD-ADP-0007", "ROVER", "START_ROVER");
    require(adapter.apply(start, engine.execute(start)).status == VehicleAdapterStatus::Applied,
            "rover start failed");

    const auto drive = command("CMD-ADP-0008", "ROVER", "DRIVE", "x=bad|y=0");
    const auto physical = adapter.apply(drive, engine.execute(drive));
    require(physical.status == VehicleAdapterStatus::InvalidParameters, "invalid DRIVE parameters were accepted");
}

void test_vikram_does_not_fake_a_physical_adapter() {
    GroundCommandExecutionEngine engine;
    VehicleCommandAdapter adapter;
    const auto command_value = command("CMD-ADP-0009", "VIKRAM", "TRANSMIT_DATA", "science");
    const auto logical = engine.execute(command_value);
    const auto physical = adapter.apply(command_value, logical);
    require(physical.status == VehicleAdapterStatus::UnsupportedTarget,
            "VIKRAM incorrectly claimed to have a physical adapter");
}

} // namespace

int main() {
    try {
        test_start_rover_hits_real_surface_rover_interface();
        test_drive_advances_physical_state();
        test_stop_affects_real_rover_state();
        test_drive_requires_numeric_coordinates();
        test_vikram_does_not_fake_a_physical_adapter();
        std::cout
            << "TRISHULA V0.9.54 - Vehicle Command Adapter Layer\n"
            << "======================================================\n"
            << "  START_ROVER reaches SurfaceRover         : PASS\n"
            << "  DRIVE advances physical rover state     : PASS\n"
            << "  STOP_ROVER affects physical rover       : PASS\n"
            << "  invalid DRIVE parameters rejected        : PASS\n"
            << "  unsupported Vikram target remains honest : PASS\n"
            << "  campaign                                 : PASS\n\n"
            << "V0.9.54 vehicle command adapter campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.54 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
