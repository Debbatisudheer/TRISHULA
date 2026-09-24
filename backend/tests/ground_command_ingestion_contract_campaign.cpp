#include <iostream>
#include <stdexcept>
#include <string>
#include "trishula/ground/command_ingestion.h"
#include "trishula/ground/space_link.h"

namespace {
using namespace trishula;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

GroundPacket make_command(std::uint32_t sequence, const std::string& payload) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Command;
    packet.header.priority = GroundPacketPriority::High;
    packet.header.source_node = "MISSION_CONTROL";
    packet.header.origin_node = "GROUND";
    packet.header.destination_node = "VIKRAM";
    packet.header.application_id = 210U;
    packet.header.mission_timestamp_ns = 1000000000ULL + sequence;
    packet.header.sequence_number = sequence;
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

void test_encoded_command_contract() {
    const auto packet = make_command(
        100U,
        "command_id=CMD-0100|target=VIKRAM|opcode=ENTER_SAFE_MODE|parameters=reason=GROUND_TEST");

    SpaceLinkFrame frame{};
    std::string error;
    require(encode_frame(packet, 900U, frame, error), "command frame encoding failed");
    require(validate_frame(frame), "command frame CRC validation failed");

    GroundPacket decoded{};
    require(decode_packet(frame.packet_bytes, decoded, error), "command frame decode failed");

    CommandIngestionPipeline pipeline;
    require(pipeline.ingest(decoded, error) == CommandIngestStatus::Accepted,
            "decoded command was rejected");

    GroundCommandRecord command{};
    require(pipeline.pop_command(command), "accepted command was not queued");
    require(command.command_id == "CMD-0100", "command id was not preserved");
    require(command.target == "VIKRAM", "command target was not normalized");
    require(command.opcode == "ENTER_SAFE_MODE", "command opcode was not normalized");
    require(command.parameters == "reason=GROUND_TEST", "command parameters were not preserved");
}

void test_command_telemetry_boundary() {
    CommandIngestionPipeline pipeline;
    std::string error;
    auto packet = make_command(101U, "command_id=CMD-0101|target=VIKRAM|opcode=STOP");
    packet.header.type = GroundPacketType::Telemetry;
    require(pipeline.ingest(packet, error) == CommandIngestStatus::UnsupportedPacketType,
            "telemetry packet crossed command-ingestion boundary");
    require(pipeline.archived_commands() == 0U, "unsupported telemetry was archived as a command");
}

} // namespace

int main() {
    try {
        test_encoded_command_contract();
        test_command_telemetry_boundary();
        std::cout
            << "TRISHULA V0.9.52 - Ground Command Ingestion Contract\n"
            << "=======================================================\n"
            << "  encoded command decode + ingestion : PASS\n"
            << "  command fields preserved           : PASS\n"
            << "  command/telemetry boundary         : PASS\n"
            << "  campaign                            : PASS\n\n"
            << "V0.9.52 command-ingestion contract campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.52 contract campaign failed: " << error.what() << '\n';
        return 1;
    }
}
