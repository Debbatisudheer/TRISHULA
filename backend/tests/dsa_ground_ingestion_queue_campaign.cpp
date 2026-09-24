#include <iostream>
#include <stdexcept>
#include <string>

#include "trishula/ground/command_ingestion.h"
#include "trishula/ground/event_ingestion.h"
#include "trishula/ground/telemetry_ingestion.h"

namespace {
using namespace trishula;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

GroundPacket telemetry_packet(std::uint32_t sequence) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Telemetry;
    packet.header.source_node = "VIKRAM";
    packet.header.origin_node = "VIKRAM";
    packet.header.destination_node = "GS-TRISHULA-01";
    packet.header.application_id = 101U;
    packet.header.sequence_number = sequence;
    const std::string payload = "metric=temperature|value=" + std::to_string(sequence) + "|unit=K|subsystem=thermal";
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

GroundPacket event_packet(std::uint32_t sequence) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Event;
    packet.header.source_node = "VIKRAM";
    packet.header.origin_node = "VIKRAM";
    packet.header.destination_node = "GS-TRISHULA-01";
    packet.header.application_id = 301U;
    packet.header.sequence_number = sequence;
    const std::string payload = "code=EVT-" + std::to_string(sequence) + "|subsystem=thermal|message=event-" +
                                 std::to_string(sequence);
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

GroundPacket command_packet(std::uint32_t sequence) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Command;
    packet.header.source_node = "MISSION_CONTROL";
    packet.header.origin_node = "GROUND";
    packet.header.destination_node = "ROVER";
    packet.header.application_id = 210U;
    packet.header.sequence_number = sequence;
    const std::string payload = "command_id=CMD-" + std::to_string(sequence) + "|target=ROVER|opcode=STOP";
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

void test_telemetry_fifo() {
    TelemetryIngestionPipeline pipeline(TelemetryIngestionConfiguration{4U, true, true});
    std::string error;
    for (std::uint32_t i = 1U; i <= 4U; ++i) {
        require(pipeline.ingest(telemetry_packet(i), error) == TelemetryIngestStatus::Accepted,
                "telemetry enqueue failed");
    }
    for (std::uint32_t expected = 1U; expected <= 4U; ++expected) {
        TelemetryRecord record{};
        require(pipeline.pop_record(record), "telemetry dequeue failed");
        require(record.sequence_number == expected, "telemetry FIFO order changed");
    }
    require(pipeline.archived_records() == 0U, "telemetry archive did not drain");
}

void test_event_fifo() {
    EventIngestionPipeline pipeline(EventIngestionConfiguration{4U, true, true});
    std::string error;
    for (std::uint32_t i = 1U; i <= 4U; ++i) {
        require(pipeline.ingest(event_packet(i), error) == EventIngestStatus::Accepted,
                "event enqueue failed");
    }
    for (std::uint32_t expected = 1U; expected <= 4U; ++expected) {
        GroundEventRecord event{};
        require(pipeline.pop_event(event), "event dequeue failed");
        require(event.sequence_number == expected, "event FIFO order changed");
    }
    require(pipeline.archived_events() == 0U, "event archive did not drain");
}

void test_command_fifo() {
    CommandIngestionPipeline pipeline(CommandIngestionConfiguration{4U, true, true, false, {"ROVER"}, {"STOP"}});
    std::string error;
    for (std::uint32_t i = 1U; i <= 4U; ++i) {
        require(pipeline.ingest(command_packet(i), error) == CommandIngestStatus::Accepted,
                "command enqueue failed");
    }
    for (std::uint32_t expected = 1U; expected <= 4U; ++expected) {
        GroundCommandRecord command{};
        require(pipeline.pop_command(command), "command dequeue failed");
        require(command.sequence_number == expected, "command FIFO order changed");
    }
    require(pipeline.archived_commands() == 0U, "command archive did not drain");
}

} // namespace

int main() {
    try {
        test_telemetry_fifo();
        test_event_fifo();
        test_command_fifo();
        std::cout
            << "TRISHULA V0.9.106.1 - Ground Ingestion Queue DSA Campaign\n"
            << "============================================================\n"
            << "  telemetry FIFO preservation : PASS\n"
            << "  event FIFO preservation     : PASS\n"
            << "  command FIFO preservation   : PASS\n"
            << "  queue-drain semantics       : PASS\n"
            << "  campaign                    : PASS\n\n"
            << "V0.9.106.1 ground ingestion queue DSA campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.106.1 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
