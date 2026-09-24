#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "trishula/core/simulation_engine.h"
#include "trishula/environment/celestial_body.h"
#include "trishula/ground/spacecraft_command_round_trip.h"
#include "trishula/physics/inertia.h"

namespace {
using namespace trishula;

void require(const bool condition, const char* message) {
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
    value.command_id = "CMD-V051-" + std::to_string(sequence);
    value.source_node = "GROUND-OPS-01";
    value.destination_node = "VIKRAM";
    value.target = "VIKRAM";
    value.opcode = opcode;
    value.parameters = parameters;
    value.priority = GroundPacketPriority::High;
    value.application_id = 502U;
    value.mission_timestamp_ns = 2000000ULL + sequence;
    value.sequence_number = sequence;
    return value;
}

void test_command_crosses_uplink_and_downlink() {
    auto simulation = make_simulation();
    GroundSpacecraftCommandRoundTrip round_trip;
    const auto before = simulation.spacecraft().state();
    const auto result = round_trip.execute(
        command(1U, "MAIN_ENGINE", "throttle=0.2|dt=0.1|direction_x=1|direction_y=0|direction_z=0"), simulation);

    require(result.status == CommandRoundTripStatus::Applied, "command round trip did not apply");
    require(result.ground_packets_received == 4U, "ground did not receive acknowledgement plus telemetry");
    require(result.telemetry_records_ingested == 3U, "telemetry feedback count incorrect");
    require(simulation.spacecraft().state().time_seconds > before.time_seconds, "physical simulation did not advance");
    require(std::abs(simulation.spacecraft().state().velocity_m_per_s.x - before.velocity_m_per_s.x) > 1.0e-9,
            "physical state did not respond to command");
    require(round_trip.ground_station().stats().packets_accepted == 4U, "ground packet acceptance count incorrect");
}

void test_acknowledgement_is_received_at_ground() {
    auto simulation = make_simulation();
    GroundSpacecraftCommandRoundTrip round_trip;
    const auto result = round_trip.execute(command(2U, "COAST", "dt=0.2"), simulation);
    require(result.status == CommandRoundTripStatus::Applied, "COAST round trip failed");
    require(result.acknowledgement_packet.header.type == GroundPacketType::Event, "acknowledgement is not an event packet");
    require(std::string(result.acknowledgement_packet.payload.begin(), result.acknowledgement_packet.payload.end()).find("status=APPLIED") != std::string::npos,
            "acknowledgement does not report physical application");
}

void test_invalid_command_does_not_claim_applied() {
    auto simulation = make_simulation();
    GroundSpacecraftCommandRoundTrip round_trip;
    const auto before = simulation.spacecraft().state();
    const auto result = round_trip.execute(command(3U, "MAIN_ENGINE", "throttle=2|dt=0.1|direction_x=1|direction_y=0|direction_z=0"), simulation);
    require(result.status == CommandRoundTripStatus::SpacecraftRejected, "invalid command was claimed applied");
    require(result.spacecraft_result.status == SpacecraftCommandStatus::InvalidParameters, "invalid command status incorrect");
    require(std::abs(simulation.clock().time() - before.time_seconds) < 1.0e-12, "invalid command changed physical state");
}

void test_ground_telemetry_reflects_physical_state() {
    auto simulation = make_simulation();
    GroundSpacecraftCommandRoundTrip round_trip;
    const auto result = round_trip.execute(command(4U, "MAIN_ENGINE", "throttle=0.1|dt=0.1|direction_x=1|direction_y=0|direction_z=0"), simulation);
    require(result.status == CommandRoundTripStatus::Applied, "telemetry round trip failed");

    TelemetryRecord speed{};
    require(round_trip.telemetry_pipeline().get_current("VIKRAM", "VIKRAM", "spacecraft_speed_m_s", speed),
            "spacecraft speed telemetry missing");
    require(std::isfinite(speed.value) && speed.value > 0.0, "spacecraft speed telemetry is not physical");
}

} // namespace

int main() {
    try {
        test_command_crosses_uplink_and_downlink();
        test_acknowledgement_is_received_at_ground();
        test_invalid_command_does_not_claim_applied();
        test_ground_telemetry_reflects_physical_state();
        std::cout
            << "TRISHULA V0.9.51 - Ground/Spacecraft Command Round Trip\n"
            << "=====================================================\n"
            << "  command crosses encoded uplink/downlink boundary : PASS\n"
            << "  spacecraft acknowledgement reaches ground       : PASS\n"
            << "  invalid command cannot claim physical apply     : PASS\n"
            << "  ground telemetry reflects physical spacecraft   : PASS\n"
            << "  campaign                                          : PASS\n\n"
            << "V0.9.51 ground/spacecraft round-trip campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.51 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
