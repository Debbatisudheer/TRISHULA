#include "trishula/ground/mission_data_model.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_set>

namespace trishula {
namespace {
constexpr std::string_view kMagic = "TRISHULA-GDS";
constexpr std::uint16_t kSupportedSchemaVersion = 1U;

std::string percent_encode(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (const unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '/') {
            out << static_cast<char>(ch);
        } else {
            out << '%' << std::setw(2) << static_cast<unsigned>(ch);
        }
    }
    return out.str();
}

bool hex_value(const char c, unsigned& value) noexcept {
    if (c >= '0' && c <= '9') { value = static_cast<unsigned>(c - '0'); return true; }
    if (c >= 'A' && c <= 'F') { value = static_cast<unsigned>(c - 'A' + 10); return true; }
    if (c >= 'a' && c <= 'f') { value = static_cast<unsigned>(c - 'a' + 10); return true; }
    return false;
}

bool percent_decode(const std::string& value, std::string& decoded) {
    decoded.clear();
    decoded.reserve(value.size());
    for (std::size_t i = 0U; i < value.size(); ++i) {
        if (value[i] != '%') {
            decoded.push_back(value[i]);
            continue;
        }
        if (i + 2U >= value.size()) return false;
        unsigned high = 0U;
        unsigned low = 0U;
        if (!hex_value(value[i + 1U], high) || !hex_value(value[i + 2U], low)) return false;
        decoded.push_back(static_cast<char>((high << 4U) | low));
        i += 2U;
    }
    return true;
}

std::string kind_name(const GroundDataKind kind) {
    switch (kind) {
        case GroundDataKind::Telemetry: return "telemetry";
        case GroundDataKind::Science: return "science";
        case GroundDataKind::Event: return "event";
        case GroundDataKind::Command: return "command";
        case GroundDataKind::File: return "file";
    }
    return "unknown";
}

std::string priority_name(const GroundPacketPriority priority) {
    switch (priority) {
        case GroundPacketPriority::Low: return "low";
        case GroundPacketPriority::Normal: return "normal";
        case GroundPacketPriority::High: return "high";
        case GroundPacketPriority::Critical: return "critical";
    }
    return "normal";
}

bool parse_priority(const std::string& value, GroundPacketPriority& priority) noexcept {
    if (value == "low") { priority = GroundPacketPriority::Low; return true; }
    if (value == "normal") { priority = GroundPacketPriority::Normal; return true; }
    if (value == "high") { priority = GroundPacketPriority::High; return true; }
    if (value == "critical") { priority = GroundPacketPriority::Critical; return true; }
    return false;
}

std::string event_kind_name(const EventKind kind) {
    switch (kind) {
        case EventKind::MissionEvent: return "mission_event";
        case EventKind::Fault: return "fault";
        case EventKind::Recovery: return "recovery";
    }
    return "mission_event";
}

std::string severity_name(const EventSeverity severity) {
    switch (severity) {
        case EventSeverity::Info: return "info";
        case EventSeverity::Warning: return "warning";
        case EventSeverity::Error: return "error";
        case EventSeverity::Critical: return "critical";
    }
    return "info";
}

std::string science_status_name(const ScienceDataProductStatus status) {
    switch (status) {
        case ScienceDataProductStatus::Draft: return "draft";
        case ScienceDataProductStatus::Validated: return "validated";
        case ScienceDataProductStatus::Stored: return "stored";
        case ScienceDataProductStatus::Rejected: return "rejected";
    }
    return "draft";
}

std::string to_string_precise(const double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

bool finite_quality(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

void add_field(std::vector<std::pair<std::string, std::string>>& fields,
               std::string key,
               std::string value) {
    fields.emplace_back(std::move(key), std::move(value));
}

bool parse_uint64(const std::string& value, std::uint64_t& out) noexcept {
    try {
        std::size_t consumed = 0U;
        const auto parsed = std::stoull(value, &consumed);
        if (consumed != value.size()) return false;
        out = static_cast<std::uint64_t>(parsed);
        return true;
    } catch (...) { return false; }
}

bool parse_uint16(const std::string& value, std::uint16_t& out) noexcept {
    std::uint64_t parsed = 0U;
    if (!parse_uint64(value, parsed) || parsed > 65535U) return false;
    out = static_cast<std::uint16_t>(parsed);
    return true;
}

bool parse_double(const std::string& value, double& out) noexcept {
    try {
        std::size_t consumed = 0U;
        out = std::stod(value, &consumed);
        return consumed == value.size() && std::isfinite(out);
    } catch (...) { return false; }
}

bool split_wire(const std::string& encoded, std::vector<std::pair<std::string, std::string>>& tokens, std::string& error) {
    std::size_t start = 0U;
    while (start <= encoded.size()) {
        const std::size_t end = encoded.find('|', start);
        const std::string token = encoded.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (token.empty()) { error = "empty canonical token"; return false; }
        const std::size_t equals = token.find('=');
        if (equals == std::string::npos || equals == 0U) { error = "canonical token missing key/value separator"; return false; }
        std::string key{};
        std::string value{};
        if (!percent_decode(token.substr(0U, equals), key) || !percent_decode(token.substr(equals + 1U), value)) {
            error = "invalid percent encoding in canonical record";
            return false;
        }
        tokens.emplace_back(std::move(key), std::move(value));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return true;
}


} // namespace

GroundDataKind ground_data_kind(const GroundPacketType type) noexcept {
    return static_cast<GroundDataKind>(static_cast<std::uint8_t>(type));
}

const char* ground_data_kind_name(const GroundDataKind kind) noexcept {
    switch (kind) {
        case GroundDataKind::Telemetry: return "telemetry";
        case GroundDataKind::Science: return "science";
        case GroundDataKind::Event: return "event";
        case GroundDataKind::Command: return "command";
        case GroundDataKind::File: return "file";
    }
    return "unknown";
}

bool parse_ground_data_kind(const std::string& value, GroundDataKind& kind) noexcept {
    if (value == "telemetry") { kind = GroundDataKind::Telemetry; return true; }
    if (value == "science") { kind = GroundDataKind::Science; return true; }
    if (value == "event") { kind = GroundDataKind::Event; return true; }
    if (value == "command") { kind = GroundDataKind::Command; return true; }
    if (value == "file") { kind = GroundDataKind::File; return true; }
    return false;
}

bool validate_ground_data_record(const GroundDataRecord& record, std::string& error) noexcept {
    error.clear();
    if (record.envelope.schema_version == 0U || record.envelope.schema_version > kSupportedSchemaVersion) {
        error = "unsupported ground data schema version"; return false;
    }
    if (record.envelope.record_id.empty()) { error = "record_id is required"; return false; }
    if (record.envelope.mission_id.empty()) { error = "mission_id is required"; return false; }
    if (record.envelope.source_node.empty() || record.envelope.origin_node.empty() || record.envelope.destination_node.empty()) {
        error = "source, origin, and destination are required"; return false;
    }
    if (record.envelope.payload_schema.empty()) { error = "payload_schema is required"; return false; }
    if (!finite_quality(record.envelope.quality)) { error = "quality must be finite and within [0,1]"; return false; }
    std::unordered_set<std::string> names;
    for (const auto& [key, value] : record.fields) {
        if (key.empty()) { error = "payload field name is empty"; return false; }
        if (!names.insert(key).second) { error = "duplicate payload field: " + key; return false; }
        if (value.size() > 4096U) { error = "payload field exceeds 4096 bytes: " + key; return false; }
    }
    return true;
}

GroundDataRecord make_ground_telemetry_record(const TelemetryRecord& telemetry,
                                              std::string record_id,
                                              std::string mission_id,
                                              std::string correlation_id) {
    GroundDataRecord record{};
    record.envelope = {1U, GroundDataKind::Telemetry, std::move(record_id), std::move(mission_id),
                       telemetry.source_node, telemetry.origin_node, telemetry.destination_node,
                       GroundPacketPriority::Normal, telemetry.application_id, telemetry.mission_timestamp_ns,
                       telemetry.sequence_number, telemetry.quality, std::move(correlation_id), "telemetry.v1"};
    add_field(record.fields, "metric", telemetry.metric);
    add_field(record.fields, "subsystem", telemetry.subsystem);
    add_field(record.fields, "unit", telemetry.unit);
    add_field(record.fields, "value", to_string_precise(telemetry.value));
    return record;
}

GroundDataRecord make_ground_event_record(const GroundEventRecord& event,
                                          std::string record_id,
                                          std::string mission_id,
                                          std::string correlation_id) {
    GroundDataRecord record{};
    record.envelope = {1U, GroundDataKind::Event, std::move(record_id), std::move(mission_id),
                       event.source_node, event.origin_node, event.destination_node,
                       event.severity == EventSeverity::Critical ? GroundPacketPriority::Critical : GroundPacketPriority::Normal,
                       event.application_id, event.mission_timestamp_ns, event.sequence_number, 1.0,
                       std::move(correlation_id), "event.v1"};
    add_field(record.fields, "kind", event_kind_name(event.kind));
    add_field(record.fields, "severity", severity_name(event.severity));
    add_field(record.fields, "code", event.code);
    add_field(record.fields, "subsystem", event.subsystem);
    add_field(record.fields, "message", event.message);
    add_field(record.fields, "active", event.active ? "true" : "false");
    return record;
}

GroundDataRecord make_ground_command_record(const GroundCommandRecord& command,
                                            std::string record_id,
                                            std::string mission_id,
                                            std::string correlation_id) {
    GroundDataRecord record{};
    record.envelope = {1U, GroundDataKind::Command, std::move(record_id), std::move(mission_id),
                       command.source_node, command.source_node, command.destination_node,
                       command.priority, command.application_id, command.mission_timestamp_ns,
                       command.sequence_number, 1.0, std::move(correlation_id), "command.v1"};
    add_field(record.fields, "command_id", command.command_id);
    add_field(record.fields, "target", command.target);
    add_field(record.fields, "opcode", command.opcode);
    add_field(record.fields, "parameters", command.parameters);
    return record;
}

GroundDataRecord make_ground_science_record(const ScienceDataProduct& science,
                                            std::string source_node,
                                            std::string origin_node,
                                            std::string destination_node,
                                            std::uint16_t application_id,
                                            std::uint64_t mission_timestamp_ns,
                                            std::string mission_id,
                                            std::string correlation_id) {
    GroundDataRecord record{};
    record.envelope = {1U, GroundDataKind::Science, science.metadata.product_id, std::move(mission_id),
                       std::move(source_node), std::move(origin_node), std::move(destination_node),
                       GroundPacketPriority::Normal, application_id, mission_timestamp_ns,
                       science.metadata.sequence, science.quality, std::move(correlation_id), "science.v1"};
    add_field(record.fields, "product_id", science.metadata.product_id);
    add_field(record.fields, "target_id", std::to_string(science.metadata.target_id));
    add_field(record.fields, "instrument", science.metadata.instrument);
    add_field(record.fields, "acquisition_time_tag", science.metadata.acquisition_time_tag);
    add_field(record.fields, "provenance", science.metadata.provenance);
    add_field(record.fields, "science_score", to_string_precise(science.science_score));
    add_field(record.fields, "energy_used_wh", to_string_precise(science.energy_used_wh));
    add_field(record.fields, "status", science_status_name(science.status));
    add_field(record.fields, "acquired", science.acquired ? "true" : "false");
    add_field(record.fields, "validated", science.validated ? "true" : "false");
    add_field(record.fields, "stored", science.stored ? "true" : "false");
    add_field(record.fields, "surface_x_m", to_string_precise(science.measurement.surface_x_m));
    add_field(record.fields, "libs_laser_energy_mj", to_string_precise(science.measurement.libs_laser_energy_mj));
    add_field(record.fields, "libs_plasma_temperature_k", to_string_precise(science.measurement.libs_plasma_temperature_k));
    add_field(record.fields, "libs_signal_to_noise", to_string_precise(science.measurement.libs_signal_to_noise));
    add_field(record.fields, "apxs_exposure_s", to_string_precise(science.measurement.apxs_exposure_s));
    add_field(record.fields, "apxs_counts_per_second", to_string_precise(science.measurement.apxs_counts_per_second));
    add_field(record.fields, "camera_brightness", to_string_precise(science.measurement.camera_brightness));
    add_field(record.fields, "camera_texture_rms", to_string_precise(science.measurement.camera_texture_rms));
    add_field(record.fields, "camera_horizon_gradient", to_string_precise(science.measurement.camera_horizon_gradient));
    add_field(record.fields, "camera_illuminated", science.measurement.camera_illuminated ? "true" : "false");
    return record;
}

std::string canonical_encode(const GroundDataRecord& record) {
    std::string error;
    if (!validate_ground_data_record(record, error)) return {};

    std::map<std::string, std::string> tokens;
    tokens.emplace("app", std::to_string(record.envelope.application_id));
    tokens.emplace("correlation_id", record.envelope.correlation_id);
    tokens.emplace("destination", record.envelope.destination_node);
    tokens.emplace("kind", kind_name(record.envelope.kind));
    tokens.emplace("mission_id", record.envelope.mission_id);
    tokens.emplace("mission_timestamp_ns", std::to_string(record.envelope.mission_timestamp_ns));
    tokens.emplace("origin", record.envelope.origin_node);
    tokens.emplace("payload_schema", record.envelope.payload_schema);
    tokens.emplace("priority", priority_name(record.envelope.priority));
    tokens.emplace("quality", to_string_precise(record.envelope.quality));
    tokens.emplace("record_id", record.envelope.record_id);
    tokens.emplace("schema", std::to_string(record.envelope.schema_version));
    tokens.emplace("sequence", std::to_string(record.envelope.sequence_number));
    tokens.emplace("source", record.envelope.source_node);

    std::ostringstream out;
    out << kMagic;
    for (const auto& [key, value] : tokens) out << '|' << percent_encode(key) << '=' << percent_encode(value);

    std::vector<std::pair<std::string, std::string>> fields = record.fields;
    std::sort(fields.begin(), fields.end());
    for (const auto& [key, value] : fields) {
        out << "|field." << percent_encode(key) << '=' << percent_encode(value);
    }
    return out.str();
}

bool canonical_decode(const std::string& encoded, GroundDataRecord& record, std::string& error) {
    record = {};
    error.clear();
    if (encoded.rfind(std::string(kMagic), 0U) != 0U) { error = "invalid canonical ground data magic"; return false; }

    std::string body = encoded.substr(kMagic.size());
    if (body.empty() || body.front() != '|') { error = "canonical record has no envelope"; return false; }
    body.erase(body.begin());

    std::vector<std::pair<std::string, std::string>> tokens;
    if (!split_wire(body, tokens, error)) return false;
    std::unordered_set<std::string> seen;
    for (const auto& [key, value] : tokens) {
        if (key.rfind("field.", 0U) == 0U) {
            const std::string field_name = key.substr(6U);
            if (field_name.empty()) { error = "empty payload field name"; return false; }
            record.fields.emplace_back(field_name, value);
            continue;
        }
        if (!seen.insert(key).second) { error = "duplicate canonical envelope key: " + key; return false; }
        if (key == "schema") {
            std::uint16_t parsed = 0U; if (!parse_uint16(value, parsed)) { error = "invalid schema"; return false; } record.envelope.schema_version = parsed;
        } else if (key == "kind") {
            if (!parse_ground_data_kind(value, record.envelope.kind)) { error = "invalid kind"; return false; }
        } else if (key == "record_id") record.envelope.record_id = value;
        else if (key == "mission_id") record.envelope.mission_id = value;
        else if (key == "source") record.envelope.source_node = value;
        else if (key == "origin") record.envelope.origin_node = value;
        else if (key == "destination") record.envelope.destination_node = value;
        else if (key == "priority") {
            if (!parse_priority(value, record.envelope.priority)) { error = "invalid priority"; return false; }
        } else if (key == "app") {
            if (!parse_uint16(value, record.envelope.application_id)) { error = "invalid application id"; return false; }
        } else if (key == "mission_timestamp_ns") {
            if (!parse_uint64(value, record.envelope.mission_timestamp_ns)) { error = "invalid mission timestamp"; return false; }
        } else if (key == "sequence") {
            if (!parse_uint64(value, record.envelope.sequence_number)) { error = "invalid sequence"; return false; }
        } else if (key == "quality") {
            if (!parse_double(value, record.envelope.quality)) { error = "invalid quality"; return false; }
        } else if (key == "correlation_id") record.envelope.correlation_id = value;
        else if (key == "payload_schema") record.envelope.payload_schema = value;
        else { error = "unknown canonical envelope key: " + key; return false; }
    }

    return validate_ground_data_record(record, error);
}

} // namespace trishula
