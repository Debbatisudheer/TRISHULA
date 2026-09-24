#include <iostream>
#include <stdexcept>
#include <string>
#include "trishula/ground/event_ingestion.h"

namespace {
using namespace trishula;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

GroundPacket event_packet(std::uint32_t sequence, const std::string& payload) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Event;
    packet.header.priority = GroundPacketPriority::High;
    packet.header.source_node = "VIKRAM";
    packet.header.origin_node = "ROVER";
    packet.header.destination_node = "GS-TRISHULA-01";
    packet.header.application_id = 150U;
    packet.header.mission_timestamp_ns = 700000000ULL + sequence;
    packet.header.sequence_number = sequence;
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

void test_fault_and_recovery() {
    EventIngestionPipeline pipeline;
    std::string error;
    require(pipeline.ingest(event_packet(1U, "kind=fault|severity=3|code=MOTOR_FAULT|subsystem=mobility|message=left wheel controller fault|active=1"), error) == EventIngestStatus::Accepted, "fault rejected");
    GroundEventRecord fault{};
    require(pipeline.get_active_fault("ROVER", "MOTOR_FAULT", fault), "active fault missing");
    require(fault.severity == EventSeverity::Critical, "fault severity incorrect");
    require(fault.active, "fault should be active");
    require(pipeline.active_faults() == 1U, "active fault count incorrect");

    require(pipeline.ingest(event_packet(2U, "kind=recovery|severity=1|code=MOTOR_FAULT|subsystem=mobility|message=left wheel controller recovered"), error) == EventIngestStatus::Accepted, "recovery rejected");
    require(!pipeline.get_active_fault("ROVER", "MOTOR_FAULT", fault), "fault remained active after recovery");
    require(pipeline.stats().recovery_events == 1U, "recovery counter incorrect");
}

void test_event_boundary_and_ordering() {
    EventIngestionPipeline pipeline;
    std::string error;
    require(pipeline.ingest(event_packet(10U, "kind=event|severity=0|code=SCIENCE_STARTED|subsystem=science|message=LIBS observation started"), error) == EventIngestStatus::Accepted, "mission event rejected");
    require(pipeline.ingest(event_packet(10U, "kind=event|severity=0|code=SCIENCE_STARTED|subsystem=science|message=duplicate"), error) == EventIngestStatus::Duplicate, "duplicate event accepted");
    require(pipeline.ingest(event_packet(9U, "kind=event|severity=1|code=OLD_EVENT|subsystem=science|message=old event"), error) == EventIngestStatus::OutOfOrder, "out-of-order event accepted");

    GroundPacket telemetry = event_packet(11U, "kind=event|severity=0|code=BATTERY_NORMAL|subsystem=power|message=normal");
    telemetry.header.type = GroundPacketType::Telemetry;
    require(pipeline.ingest(telemetry, error) == EventIngestStatus::UnsupportedPacketType, "telemetry crossed event boundary");
    require(pipeline.stats().duplicate_packets == 1U, "duplicate count incorrect");
    require(pipeline.stats().out_of_order_packets == 1U, "out-of-order count incorrect");
}

void test_invalid_and_capacity() {
    EventIngestionPipeline pipeline(EventIngestionConfiguration{1U, true, true});
    std::string error;
    require(pipeline.ingest(event_packet(1U, "kind=fault|severity=3|code=THERMAL_HIGH|subsystem=thermal|message=temp high"), error) == EventIngestStatus::Accepted, "first event rejected");
    require(pipeline.ingest(event_packet(2U, "kind=fault|severity=x|code=THERMAL_LOW|subsystem=thermal|message=temp low"), error) == EventIngestStatus::InvalidPayload, "invalid event accepted");
    require(pipeline.ingest(event_packet(2U, "kind=event|severity=1|code=SECOND|subsystem=flight|message=second event"), error) == EventIngestStatus::CapacityFull, "capacity overflow not rejected");
}

} // namespace

int main() {
    try {
        test_fault_and_recovery();
        test_event_boundary_and_ordering();
        test_invalid_and_capacity();
        std::cout
            << "TRISHULA V0.9.51 - Ground Event & Fault Ingestion Pipeline\n"
            << "===============================================================\n"
            << "  fault ingestion and active-fault state : PASS\n"
            << "  recovery clears active fault            : PASS\n"
            << "  mission event ingestion                 : PASS\n"
            << "  duplicate/out-of-order detection       : PASS\n"
            << "  telemetry separation                    : PASS\n"
            << "  invalid payload/capacity handling       : PASS\n"
            << "  campaign                                 : PASS\n\n"
            << "V0.9.51 ground event ingestion campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.51 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
