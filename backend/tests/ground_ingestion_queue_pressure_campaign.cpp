#include <cmath>
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

void require_close(double actual, double expected, const char* message) {
    if (std::abs(actual - expected) > 1.0e-12) throw std::runtime_error(message);
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
    const std::string payload = "code=EVT-" + std::to_string(sequence) + "|subsystem=thermal|message=event-" + std::to_string(sequence);
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

void test_telemetry_pressure() {
    TelemetryIngestionPipeline pipeline(TelemetryIngestionConfiguration{4U, true, true});
    require(pipeline.available_capacity() == 4U, "telemetry initial available capacity incorrect");
    require_close(pipeline.capacity_utilization(), 0.0, "telemetry initial utilization incorrect");

    std::string error;
    require(pipeline.ingest(telemetry_packet(1U), error) == TelemetryIngestStatus::Accepted, "telemetry ingest failed");
    require(pipeline.ingest(telemetry_packet(2U), error) == TelemetryIngestStatus::Accepted, "telemetry ingest failed");
    require(pipeline.available_capacity() == 2U, "telemetry available capacity incorrect");
    require_close(pipeline.capacity_utilization(), 0.5, "telemetry utilization incorrect");

    TelemetryRecord record{};
    require(pipeline.pop_record(record), "telemetry pop failed");
    require(pipeline.available_capacity() == 3U, "telemetry recovery capacity incorrect");
    require_close(pipeline.capacity_utilization(), 0.25, "telemetry recovery utilization incorrect");
}

void test_event_pressure() {
    EventIngestionPipeline pipeline(EventIngestionConfiguration{2U, true, true});
    require(pipeline.available_capacity() == 2U, "event initial available capacity incorrect");
    std::string error;
    require(pipeline.ingest(event_packet(1U), error) == EventIngestStatus::Accepted, "event ingest failed");
    require(pipeline.ingest(event_packet(2U), error) == EventIngestStatus::Accepted, "event ingest failed");
    require(pipeline.available_capacity() == 0U, "event full available capacity incorrect");
    require_close(pipeline.capacity_utilization(), 1.0, "event full utilization incorrect");

    GroundEventRecord event{};
    require(pipeline.pop_event(event), "event pop failed");
    require(pipeline.available_capacity() == 1U, "event recovery capacity incorrect");
}

void test_command_pressure() {
    CommandIngestionPipeline pipeline(CommandIngestionConfiguration{2U, true, true, false, {"ROVER"}, {"STOP"}});
    require(pipeline.available_capacity() == 2U, "command initial available capacity incorrect");
    std::string error;
    require(pipeline.ingest(command_packet(1U), error) == CommandIngestStatus::Accepted, "command ingest failed");
    require(pipeline.available_capacity() == 1U, "command available capacity incorrect");
    require_close(pipeline.capacity_utilization(), 0.5, "command utilization incorrect");
    require(pipeline.ingest(command_packet(2U), error) == CommandIngestStatus::Accepted, "command ingest failed");
    require(pipeline.available_capacity() == 0U, "command full available capacity incorrect");

    GroundCommandRecord command{};
    require(pipeline.pop_command(command), "command pop failed");
    require(pipeline.available_capacity() == 1U, "command recovery capacity incorrect");
}

void test_zero_capacity() {
    TelemetryIngestionPipeline telemetry(TelemetryIngestionConfiguration{0U, true, true});
    EventIngestionPipeline event(EventIngestionConfiguration{0U, true, true});
    CommandIngestionPipeline command(CommandIngestionConfiguration{0U, true, true, false, {"ROVER"}, {"STOP"}});

    require(telemetry.available_capacity() == 0U, "zero-capacity telemetry availability incorrect");
    require(event.available_capacity() == 0U, "zero-capacity event availability incorrect");
    require(command.available_capacity() == 0U, "zero-capacity command availability incorrect");
    require_close(telemetry.capacity_utilization(), 1.0, "zero-capacity telemetry utilization incorrect");
    require_close(event.capacity_utilization(), 1.0, "zero-capacity event utilization incorrect");
    require_close(command.capacity_utilization(), 1.0, "zero-capacity command utilization incorrect");
}

} // namespace

int main() {
    try {
        test_telemetry_pressure();
        test_event_pressure();
        test_command_pressure();
        test_zero_capacity();
        std::cout
            << "TRISHULA V0.9.107.1 - Ground Ingestion Queue Pressure Campaign\n"
            << "===============================================================\n"
            << "  telemetry pressure metrics : PASS\n"
            << "  event pressure metrics     : PASS\n"
            << "  command pressure metrics   : PASS\n"
            << "  capacity recovery          : PASS\n"
            << "  zero-capacity safety       : PASS\n"
            << "  campaign                   : PASS\n\n"
            << "V0.9.107.1 ground ingestion queue pressure campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.107.1 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
