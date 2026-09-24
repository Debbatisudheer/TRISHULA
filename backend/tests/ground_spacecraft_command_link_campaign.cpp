#include <cmath>
#include <iostream>
#include <stdexcept>

#include "trishula/core/simulation_engine.h"
#include "trishula/ground/spacecraft_command_link.h"
#include "trishula/environment/celestial_body.h"
#include "trishula/physics/inertia.h"

namespace {
using namespace trishula;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

SimulationEngine make_simulation() {
    TrueState state{};
    state.position_meters = {7.0e6, 0.0, 0.0};
    state.velocity_m_per_s = {0.0, 7500.0, 0.0};
    state.mass_kg = 50000.0;
    return SimulationEngine(
        CelestialBody{3.986004418e14, 6.371e6, {0.0, 0.0, 0.0}},
        Spacecraft{state, DiagonalInertia{10000.0, 10000.0, 10000.0}},
        0.1,
        MainEngine{1.0e6, 320.0, 10000.0},
        RcsModule{1000.0, 220.0, 10000.0});
}

GroundCommandRecord command(std::uint32_t sequence, const char* opcode, const char* parameters) {
    GroundCommandRecord value{};
    value.command_id = "CMD-V050-" + std::to_string(sequence);
    value.source_node = "GROUND-OPS-01";
    value.destination_node = "VIKRAM";
    value.target = "VIKRAM";
    value.opcode = opcode;
    value.parameters = parameters;
    value.application_id = 501U;
    value.mission_timestamp_ns = 1000000ULL + sequence;
    value.sequence_number = sequence;
    return value;
}

void test_main_engine_command_changes_physical_state() {
    auto simulation = make_simulation();
    GroundSpacecraftCommandLink link;
    const auto before = simulation.spacecraft().state();
    const auto result = link.apply(command(1U, "MAIN_ENGINE", "throttle=0.2|dt=0.1|direction_x=1|direction_y=0|direction_z=0"), simulation);
    require(result.status == SpacecraftCommandStatus::Applied, "main-engine command was not applied");
    require(result.propagated_seconds == 0.1, "command duration was not propagated");
    require(simulation.spacecraft().state().time_seconds > before.time_seconds, "simulation clock did not advance");
    require(std::abs(simulation.spacecraft().state().velocity_m_per_s.x - before.velocity_m_per_s.x) > 1.0e-9, "physical velocity did not respond to thrust");
    require(result.propulsion.engine_firing, "propulsion feedback did not report engine firing");
}

void test_coast_is_physical_and_does_not_fake_thrust() {
    auto simulation = make_simulation();
    GroundSpacecraftCommandLink link;
    const auto result = link.apply(command(2U, "COAST", "dt=0.2"), simulation);
    require(result.status == SpacecraftCommandStatus::Applied, "coast command was not applied");
    require(!result.propulsion.engine_firing, "coast falsely reported engine firing");
    require(simulation.clock().time() > 0.19, "coast did not advance physical simulation time");
}

void test_sequence_replay_is_rejected_without_state_change() {
    auto simulation = make_simulation();
    GroundSpacecraftCommandLink link;
    require(link.apply(command(3U, "COAST", "dt=0.1"), simulation).status == SpacecraftCommandStatus::Applied,
            "initial command failed");
    const auto before = simulation.spacecraft().state();
    const auto replay = link.apply(command(3U, "MAIN_ENGINE", "throttle=1|dt=0.1|direction_x=1|direction_y=0|direction_z=0"), simulation);
    require(replay.status == SpacecraftCommandStatus::PreconditionsFailed, "replayed sequence was accepted");
    require(std::abs(simulation.clock().time() - before.time_seconds) < 1.0e-12, "replayed command changed simulation time");
}

void test_invalid_commands_are_rejected_before_physical_application() {
    auto simulation = make_simulation();
    GroundSpacecraftCommandLink link;
    const auto before = simulation.spacecraft().state();
    const auto bad = link.apply(command(4U, "MAIN_ENGINE", "throttle=2|dt=0.1|direction_x=1|direction_y=0|direction_z=0"), simulation);
    require(bad.status == SpacecraftCommandStatus::InvalidParameters, "invalid throttle was accepted");
    require(std::abs(simulation.clock().time() - before.time_seconds) < 1.0e-12, "invalid command advanced simulation");
}

void test_wrong_target_is_rejected() {
    auto simulation = make_simulation();
    GroundSpacecraftCommandLink link;
    auto value = command(5U, "COAST", "dt=0.1");
    value.target = "ROVER";
    const auto result = link.apply(value, simulation);
    require(result.status == SpacecraftCommandStatus::InvalidTarget, "wrong target was accepted");
}

} // namespace

int main() {
    try {
        test_main_engine_command_changes_physical_state();
        test_coast_is_physical_and_does_not_fake_thrust();
        test_sequence_replay_is_rejected_without_state_change();
        test_invalid_commands_are_rejected_before_physical_application();
        test_wrong_target_is_rejected();
        std::cout
            << "TRISHULA V0.9.50 - Ground-to-Spacecraft Command Boundary\n"
            << "=========================================================\n"
            << "  MAIN_ENGINE command changes physical state : PASS\n"
            << "  COAST propagates physical simulation       : PASS\n"
            << "  command sequence replay rejected           : PASS\n"
            << "  invalid command rejected before actuation  : PASS\n"
            << "  wrong spacecraft target rejected           : PASS\n"
            << "  campaign                                    : PASS\n\n"
            << "V0.9.50 ground-to-spacecraft command campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.50 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
