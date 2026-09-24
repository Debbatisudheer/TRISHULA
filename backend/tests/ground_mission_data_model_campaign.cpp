#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "trishula/ground/mission_data_model.h"

namespace {
using namespace trishula;

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

TelemetryRecord make_telemetry() {
    TelemetryRecord telemetry{};
    telemetry.source_node = "VIKRAM";
    telemetry.origin_node = "ROVER";
    telemetry.destination_node = "GS-TRISHULA-01";
    telemetry.application_id = 101U;
    telemetry.mission_timestamp_ns = 900000123ULL;
    telemetry.sequence_number = 44U;
    telemetry.subsystem = "thermal";
    telemetry.metric = "temperature";
    telemetry.value = 275.2;
    telemetry.unit = "K";
    telemetry.quality = 0.98;
    return telemetry;
}

GroundEventRecord make_event() {
    GroundEventRecord event{};
    event.source_node = "VIKRAM";
    event.origin_node = "ROVER";
    event.destination_node = "GS-TRISHULA-01";
    event.application_id = 201U;
    event.mission_timestamp_ns = 900000124ULL;
    event.sequence_number = 55U;
    event.kind = EventKind::Fault;
    event.severity = EventSeverity::Critical;
    event.code = "MOTOR_FAULT";
    event.subsystem = "mobility";
    event.message = "left wheel controller fault | retry pending";
    event.active = true;
    return event;
}

GroundCommandRecord make_command() {
    GroundCommandRecord command{};
    command.command_id = "CMD-001";
    command.source_node = "MISSION_CONTROL";
    command.destination_node = "ROVER";
    command.target = "ROVER";
    command.opcode = "DRIVE";
    command.parameters = "x=25|y=0|dt=0.1";
    command.priority = GroundPacketPriority::High;
    command.application_id = 220U;
    command.mission_timestamp_ns = 900000125ULL;
    command.sequence_number = 56U;
    return command;
}

void test_common_envelope_and_kind() {
    const auto telemetry = make_ground_telemetry_record(make_telemetry(), "TEL-0001", "TRISHULA-LUNAR", "CMD-001");
    require(telemetry.envelope.schema_version == 1U, "telemetry schema version incorrect");
    require(telemetry.envelope.kind == GroundDataKind::Telemetry, "telemetry kind incorrect");
    require(telemetry.envelope.record_id == "TEL-0001", "record id not preserved");
    require(telemetry.envelope.correlation_id == "CMD-001", "correlation id not preserved");
    std::string error;
    require(validate_ground_data_record(telemetry, error), error.c_str());
}

void test_canonical_round_trip_telemetry_and_special_characters() {
    const auto original = make_ground_telemetry_record(make_telemetry(), "TEL-0002", "TRISHULA-LUNAR", "CMD|SPECIAL");
    const std::string encoded = canonical_encode(original);
    require(!encoded.empty(), "canonical telemetry encoding failed");
    require(encoded.find("TRISHULA-GDS") == 0U, "canonical magic missing");

    GroundDataRecord decoded{};
    std::string error;
    require(canonical_decode(encoded, decoded, error), error.c_str());
    require(decoded.envelope.record_id == original.envelope.record_id, "telemetry record id changed");
    require(decoded.envelope.correlation_id == "CMD|SPECIAL", "special character was not preserved");
    require(decoded.fields.size() == original.fields.size(), "telemetry field count changed");
}

void test_event_and_command_share_contract() {
    const auto event = make_ground_event_record(make_event(), "EVT-0001");
    const auto command = make_ground_command_record(make_command(), "CMD-REC-0001");
    require(event.envelope.kind == GroundDataKind::Event, "event kind incorrect");
    require(command.envelope.kind == GroundDataKind::Command, "command kind incorrect");
    require(event.envelope.schema_version == command.envelope.schema_version, "schema versions diverged");
    require(event.envelope.mission_id == command.envelope.mission_id, "mission identifiers diverged");
    std::string error;
    GroundDataRecord decoded_event{};
    GroundDataRecord decoded_command{};
    require(canonical_decode(canonical_encode(event), decoded_event, error), error.c_str());
    require(canonical_decode(canonical_encode(command), decoded_command, error), error.c_str());
    require(decoded_event.envelope.kind == GroundDataKind::Event, "decoded event kind changed");
    require(decoded_command.envelope.kind == GroundDataKind::Command, "decoded command kind changed");
}

void test_science_mapping() {
    ScienceDataProduct science{};
    science.metadata.product_id = "TRS-7-000099";
    science.metadata.target_id = 7U;
    science.metadata.sequence = 99U;
    science.metadata.instrument = "LIBS+APXS";
    science.metadata.acquisition_time_tag = "2026-09-16T00:00:01Z";
    science.metadata.provenance = "TRISHULA rover science acquisition";
    science.quality = 0.94;
    science.science_score = 0.88;
    science.energy_used_wh = 12.5;
    science.status = ScienceDataProductStatus::Stored;
    science.acquired = true;
    science.validated = true;
    science.stored = true;
    science.measurement.surface_x_m = 18.25;
    science.measurement.libs_plasma_temperature_k = 9520.78;

    const auto record = make_ground_science_record(science, "VIKRAM", "ROVER", "GS-TRISHULA-01", 103U, 900000126ULL);
    require(record.envelope.kind == GroundDataKind::Science, "science kind incorrect");
    require(std::fabs(record.envelope.quality - 0.94) < 1e-12, "science quality changed");
    const std::string encoded = canonical_encode(record);
    GroundDataRecord decoded{};
    std::string error;
    require(canonical_decode(encoded, decoded, error), error.c_str());
    require(decoded.envelope.record_id == "TRS-7-000099", "science product id changed");
    require(decoded.envelope.sequence_number == 99U, "science sequence changed");
}

void test_validation_rejects_duplicate_fields() {
    GroundDataRecord record{};
    record.envelope.record_id = "BAD";
    record.envelope.mission_id = "TRISHULA";
    record.envelope.source_node = "ROVER";
    record.envelope.origin_node = "ROVER";
    record.envelope.destination_node = "GROUND";
    record.envelope.payload_schema = "telemetry.v1";
    record.fields = {{"metric", "temperature"}, {"metric", "voltage"}};
    std::string error;
    require(!validate_ground_data_record(record, error), "duplicate fields were accepted");
}

} // namespace

int main() {
    try {
        test_common_envelope_and_kind();
        test_canonical_round_trip_telemetry_and_special_characters();
        test_event_and_command_share_contract();
        test_science_mapping();
        test_validation_rejects_duplicate_fields();
        std::cout
            << "TRISHULA V0.9.56 - Ground Mission Data Model & Unified Schemas\n"
            << "====================================================================\n"
            << "  common envelope and data kinds           : PASS\n"
            << "  canonical telemetry round trip          : PASS\n"
            << "  event/command shared contract            : PASS\n"
            << "  science mapping and round trip           : PASS\n"
            << "  duplicate-field validation              : PASS\n"
            << "  campaign                                 : PASS\n\n"
            << "V0.9.56 ground mission data model campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "V0.9.56 campaign failed: " << error.what() << '\n';
        return 1;
    }
}
