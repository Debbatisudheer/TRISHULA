#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include "trishula/ground/telemetry_ingestion.h"

namespace {
using namespace trishula;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

GroundPacket telemetry(std::uint32_t sequence, const std::string& payload) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Telemetry;
    packet.header.priority = GroundPacketPriority::Normal;
    packet.header.source_node = "VIKRAM";
    packet.header.origin_node = "VIKRAM";
    packet.header.destination_node = "GS-TRISHULA-01";
    packet.header.application_id = 101U;
    packet.header.mission_timestamp_ns = 900000000ULL + sequence;
    packet.header.sequence_number = sequence;
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

void test_structured_ingestion() {
    TelemetryIngestionPipeline pipeline;
    std::string error;
    const auto packet = telemetry(1U, "metric=temperature|value=275.2|unit=K|subsystem=thermal|quality=0.98");
    require(pipeline.ingest(packet, error) == TelemetryIngestStatus::Accepted, "structured telemetry rejected");
    require(pipeline.archived_records() == 1U, "telemetry record not archived");
    TelemetryRecord current{};
    require(pipeline.get_current("VIKRAM", "VIKRAM", "temperature", current), "current telemetry missing");
    require(std::fabs(current.value - 275.2) < 1e-9, "temperature value changed");
    require(current.unit == "K", "unit not preserved");
    require(current.subsystem == "thermal", "subsystem not preserved");
    require(std::fabs(current.quality - 0.98) < 1e-9, "quality not preserved");
}

void test_compact_and_rejection() {
    TelemetryIngestionPipeline pipeline;
    std::string error;
    require(pipeline.ingest(telemetry(1U, "battery=91.2"), error) == TelemetryIngestStatus::Accepted, "compact telemetry rejected");
    GroundPacket science = telemetry(2U, "not telemetry");
    science.header.type = GroundPacketType::Science;
    require(pipeline.ingest(science, error) == TelemetryIngestStatus::UnsupportedPacketType, "science packet crossed telemetry boundary");
    require(pipeline.ingest(2U ? telemetry(2U, "metric=temperature|value=bad") : telemetry(2U, ""), error) == TelemetryIngestStatus::InvalidPayload, "invalid telemetry payload accepted");
}

void test_duplicate_order_and_current_state() {
    TelemetryIngestionPipeline pipeline;
    std::string error;
    require(pipeline.ingest(telemetry(10U, "metric=voltage|value=28.4|unit=V|subsystem=power"), error) == TelemetryIngestStatus::Accepted, "first voltage rejected");
    require(pipeline.ingest(telemetry(10U, "metric=voltage|value=28.4|unit=V|subsystem=power"), error) == TelemetryIngestStatus::Duplicate, "duplicate telemetry accepted");
    require(pipeline.ingest(telemetry(9U, "metric=voltage|value=28.3|unit=V|subsystem=power"), error) == TelemetryIngestStatus::OutOfOrder, "out-of-order telemetry accepted");
    require(pipeline.ingest(telemetry(11U, "metric=voltage|value=28.1|unit=V|subsystem=power"), error) == TelemetryIngestStatus::Accepted, "new telemetry rejected");
    TelemetryRecord current{};
    require(pipeline.get_current("VIKRAM", "VIKRAM", "voltage", current), "voltage current-state missing");
    require(std::fabs(current.value - 28.1) < 1e-9, "current-state did not advance");
    require(pipeline.stats().packets_accepted == 2U, "accepted telemetry count incorrect");
    require(pipeline.stats().duplicate_packets == 1U, "duplicate count incorrect");
    require(pipeline.stats().out_of_order_packets == 1U, "out-of-order count incorrect");
}

} // namespace

int main() {
    try {
        test_structured_ingestion();
        test_compact_and_rejection();
        test_duplicate_order_and_current_state();
        std::cout
            << "TRISHULA V0.9.50 - Ground Telemetry Ingestion Pipeline\n"
            << "===========================================================\n"
            << "  structured telemetry normalization : PASS\n"
            << "  compact telemetry compatibility    : PASS\n"
            << "  non-telemetry separation           : PASS\n"
            << "  invalid payload rejection          : PASS\n"
            << "  duplicate/out-of-order detection   : PASS\n"
            << "  current-state projection            : PASS\n"
            << "  campaign                             : PASS\n\n"
            << "V0.9.50 ground telemetry ingestion campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.50 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
