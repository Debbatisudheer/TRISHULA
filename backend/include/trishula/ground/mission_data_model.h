#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "trishula/ground/command_ingestion.h"
#include "trishula/ground/event_ingestion.h"
#include "trishula/ground/space_link.h"
#include "trishula/ground/telemetry_ingestion.h"
#include "trishula/rover/science_data_products.h"

namespace trishula {

enum class GroundDataKind : std::uint8_t {
    Telemetry = 1,
    Science = 2,
    Event = 3,
    Command = 4,
    File = 5
};

struct GroundDataEnvelope {
    std::uint16_t schema_version{1U};
    GroundDataKind kind{GroundDataKind::Telemetry};
    std::string record_id{};
    std::string mission_id{"TRISHULA"};
    std::string source_node{};
    std::string origin_node{};
    std::string destination_node{};
    GroundPacketPriority priority{GroundPacketPriority::Normal};
    std::uint16_t application_id{0U};
    std::uint64_t mission_timestamp_ns{0U};
    std::uint64_t sequence_number{0U};
    double quality{1.0};
    std::string correlation_id{};
    std::string payload_schema{};
};

struct GroundDataRecord {
    GroundDataEnvelope envelope{};
    std::vector<std::pair<std::string, std::string>> fields{};
};

[[nodiscard]] GroundDataKind ground_data_kind(const GroundPacketType type) noexcept;
[[nodiscard]] const char* ground_data_kind_name(const GroundDataKind kind) noexcept;
[[nodiscard]] bool parse_ground_data_kind(const std::string& value, GroundDataKind& kind) noexcept;

[[nodiscard]] bool validate_ground_data_record(const GroundDataRecord& record,
                                               std::string& error) noexcept;

[[nodiscard]] GroundDataRecord make_ground_telemetry_record(const TelemetryRecord& telemetry,
                                                             std::string record_id,
                                                             std::string mission_id = "TRISHULA",
                                                             std::string correlation_id = {});

[[nodiscard]] GroundDataRecord make_ground_event_record(const GroundEventRecord& event,
                                                         std::string record_id,
                                                         std::string mission_id = "TRISHULA",
                                                         std::string correlation_id = {});

[[nodiscard]] GroundDataRecord make_ground_command_record(const GroundCommandRecord& command,
                                                           std::string record_id,
                                                           std::string mission_id = "TRISHULA",
                                                           std::string correlation_id = {});

[[nodiscard]] GroundDataRecord make_ground_science_record(const ScienceDataProduct& science,
                                                           std::string source_node = "ROVER",
                                                           std::string origin_node = "ROVER",
                                                           std::string destination_node = "GS-TRISHULA-01",
                                                           std::uint16_t application_id = 103U,
                                                           std::uint64_t mission_timestamp_ns = 0U,
                                                           std::string mission_id = "TRISHULA",
                                                           std::string correlation_id = {});

[[nodiscard]] std::string canonical_encode(const GroundDataRecord& record);
[[nodiscard]] bool canonical_decode(const std::string& encoded,
                                    GroundDataRecord& record,
                                    std::string& error);

} // namespace trishula
