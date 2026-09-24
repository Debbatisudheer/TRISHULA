#include <iostream>
#include <stdexcept>
#include <string>
#include "trishula/ground/command_ingestion.h"

namespace {
using namespace trishula;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

GroundPacket command_packet(std::uint32_t sequence, const std::string& payload) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Command;
    packet.header.priority = GroundPacketPriority::High;
    packet.header.source_node = "MISSION_CONTROL";
    packet.header.origin_node = "GROUND";
    packet.header.destination_node = "VIKRAM";
    packet.header.application_id = 210U;
    packet.header.mission_timestamp_ns = 900000000ULL + sequence;
    packet.header.sequence_number = sequence;
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

void test_accept_and_record() {
    CommandIngestionPipeline pipeline;
    std::string error;
    const auto status = pipeline.ingest(command_packet(1U,
        "command_id=CMD-0001|target=rover|opcode=START_ROVER|parameters=mode=AUTONOMOUS"), error);
    require(status == CommandIngestStatus::Accepted, "valid command rejected");
    require(error.empty(), "valid command produced error");
    require(pipeline.archived_commands() == 1U, "command archive count incorrect");
    require(pipeline.contains_command_id("CMD-0001"), "command id not tracked");

    GroundCommandRecord command{};
    require(pipeline.pop_command(command), "command was not queued");
    require(command.target == "ROVER", "target normalization failed");
    require(command.opcode == "START_ROVER", "opcode normalization failed");
    require(command.parameters == "mode=AUTONOMOUS", "parameters not retained");
}

void test_validation_boundaries() {
    CommandIngestionPipeline pipeline;
    std::string error;
    require(pipeline.ingest(command_packet(2U, "command_id=CMD-0002|target=unknown|opcode=START_ROVER"), error) ==
                CommandIngestStatus::InvalidTarget, "invalid target accepted");
    require(pipeline.ingest(command_packet(3U, "command_id=CMD-0003|target=ROVER|opcode=FORMAT_DISK"), error) ==
                CommandIngestStatus::UnknownOpcode, "unknown opcode accepted");
    require(pipeline.ingest(command_packet(4U, "target=ROVER|opcode=STOP"), error) ==
                CommandIngestStatus::InvalidPayload, "missing command id accepted");

    GroundPacket telemetry = command_packet(5U, "command_id=CMD-0005|target=ROVER|opcode=STOP");
    telemetry.header.type = GroundPacketType::Telemetry;
    require(pipeline.ingest(telemetry, error) == CommandIngestStatus::UnsupportedPacketType,
            "telemetry crossed command boundary");
}

void test_duplicate_order_and_frame() {
    CommandIngestionPipeline pipeline;
    std::string error;
    require(pipeline.ingest(command_packet(10U, "command_id=CMD-0010|target=VIKRAM|opcode=ENTER_SAFE_MODE"), error) ==
                CommandIngestStatus::Accepted, "first command rejected");
    require(pipeline.ingest(command_packet(11U, "command_id=CMD-0010|target=VIKRAM|opcode=ENTER_SAFE_MODE"), error) ==
                CommandIngestStatus::Duplicate, "duplicate command id accepted");
    require(pipeline.ingest(command_packet(9U, "command_id=CMD-0009|target=VIKRAM|opcode=STOP"), error) ==
                CommandIngestStatus::OutOfOrder, "out-of-order command accepted");

    SpaceLinkFrame frame{};
    require(encode_frame(command_packet(12U, "command_id=CMD-0012|target=VIKRAM|opcode=RESET_SUBSYSTEM|parameters=thermal"), 500U, frame, error),
            "command frame encoding failed");
    require(validate_frame(frame), "encoded command frame failed CRC validation");

    GroundPacket decoded{};
    require(decode_packet(frame.packet_bytes, decoded, error), "command frame packet decode failed");
    require(decoded.header.type == GroundPacketType::Command, "decoded packet type changed");
}

void test_capacity() {
    CommandIngestionPipeline pipeline(CommandIngestionConfiguration{1U, true, true, false, {"ROVER"}, {"STOP"}});
    std::string error;
    require(pipeline.ingest(command_packet(1U, "command_id=CMD-1001|target=ROVER|opcode=STOP"), error) ==
                CommandIngestStatus::Accepted, "capacity test first command rejected");
    require(pipeline.ingest(command_packet(2U, "command_id=CMD-1002|target=ROVER|opcode=STOP"), error) ==
                CommandIngestStatus::CapacityFull, "capacity overflow accepted");
}

} // namespace

int main() {
    try {
        test_accept_and_record();
        test_validation_boundaries();
        test_duplicate_order_and_frame();
        test_capacity();
        std::cout
            << "TRISHULA V0.9.52 - Ground Command Ingestion & Validation Pipeline\n"
            << "=====================================================================\n"
            << "  command ingestion and normalization : PASS\n"
            << "  target/opcode validation            : PASS\n"
            << "  duplicate/out-of-order detection    : PASS\n"
            << "  command frame encode/CRC validation  : PASS\n"
            << "  command/telemetry separation        : PASS\n"
            << "  capacity handling                   : PASS\n"
            << "  campaign                             : PASS\n\n"
            << "V0.9.52 ground command campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.52 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
