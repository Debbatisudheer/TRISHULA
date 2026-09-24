#include "trishula/ground/telemetry_ingestion.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace trishula {
namespace {

bool parse_double(const std::string& text, double& value) {
    char* end = nullptr;
    const char* begin = text.c_str();
    value = std::strtod(begin, &end);
    if (end == begin || *end != '\0' || !std::isfinite(value)) {
        return false;
    }
    return true;
}



} // namespace

std::size_t TelemetryIngestionPipeline::StreamKeyHash::operator()(const StreamKey& key) const noexcept {
    const auto h1 = std::hash<std::string>{}(key.source_node);
    const auto h2 = std::hash<std::string>{}(key.origin_node);
    const auto h3 = std::hash<std::uint16_t>{}(key.application_id);
    return h1 ^ (h2 << 1U) ^ (h3 << 2U);
}

std::size_t TelemetryIngestionPipeline::MetricKeyHash::operator()(const MetricKey& key) const noexcept {
    const auto h1 = std::hash<std::string>{}(key.source_node);
    const auto h2 = std::hash<std::string>{}(key.origin_node);
    const auto h3 = std::hash<std::string>{}(key.metric);
    return h1 ^ (h2 << 1U) ^ (h3 << 2U);
}

TelemetryIngestionPipeline::TelemetryIngestionPipeline(TelemetryIngestionConfiguration configuration)
    : configuration_(std::move(configuration)) {
}

const TelemetryIngestionConfiguration& TelemetryIngestionPipeline::configuration() const noexcept {
    return configuration_;
}

const TelemetryIngestionStats& TelemetryIngestionPipeline::stats() const noexcept {
    return stats_;
}

std::size_t TelemetryIngestionPipeline::archived_records() const noexcept {
    return archive_.size();
}

std::size_t TelemetryIngestionPipeline::available_capacity() const noexcept {
    if (archive_.size() >= configuration_.archive_capacity) {
        return 0U;
    }
    return configuration_.archive_capacity - archive_.size();
}

double TelemetryIngestionPipeline::capacity_utilization() const noexcept {
    if (configuration_.archive_capacity == 0U) {
        return 1.0;
    }
    return static_cast<double>(archive_.size()) /
           static_cast<double>(configuration_.archive_capacity);
}

std::string TelemetryIngestionPipeline::trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1U);
}

TelemetryIngestionPipeline::StreamKey TelemetryIngestionPipeline::stream_key(const GroundPacket& packet) {
    return {packet.header.source_node, packet.header.origin_node, packet.header.application_id};
}

bool TelemetryIngestionPipeline::parse_payload(const GroundPacket& packet, TelemetryRecord& record, std::string& error) {
    error.clear();
    record = {};
    record.source_node = packet.header.source_node;
    record.origin_node = packet.header.origin_node;
    record.destination_node = packet.header.destination_node;
    record.application_id = packet.header.application_id;
    record.mission_timestamp_ns = packet.header.mission_timestamp_ns;
    record.sequence_number = packet.header.sequence_number;

    const std::string payload(packet.payload.begin(), packet.payload.end());
    if (payload.empty()) {
        error = "telemetry payload is empty";
        return false;
    }

    std::unordered_map<std::string, std::string> fields;
    std::stringstream stream(payload);
    std::string token;
    while (std::getline(stream, token, '|')) {
        token = trim(token);
        if (token.empty()) {
            continue;
        }
        const auto separator = token.find('=');
        if (separator == std::string::npos || separator == 0U) {
            error = "telemetry payload field must use key=value: " + token;
            return false;
        }
        const auto key = trim(token.substr(0U, separator));
        const auto value = trim(token.substr(separator + 1U));
        if (key.empty() || value.empty()) {
            error = "telemetry payload contains an empty key or value";
            return false;
        }
        fields[key] = value;
    }

    // Backward-compatible compact form: "battery=91.2".
    if (fields.size() == 1U && fields.find("metric") == fields.end()) {
        const auto& entry = *fields.begin();
        double parsed_value = 0.0;
        if (!parse_double(entry.second, parsed_value)) {
            error = "compact telemetry value is not numeric";
            return false;
        }
        record.metric = entry.first;
        record.value = parsed_value;
        record.subsystem = "unknown";
        record.unit = "unknown";
        record.quality = 1.0;
        return true;
    }

    const auto metric_it = fields.find("metric");
    const auto value_it = fields.find("value");
    if (metric_it == fields.end() || value_it == fields.end()) {
        error = "structured telemetry requires metric= and value=";
        return false;
    }

    double parsed_value = 0.0;
    if (!parse_double(value_it->second, parsed_value)) {
        error = "telemetry value is not a finite number";
        return false;
    }

    record.metric = metric_it->second;
    record.value = parsed_value;
    record.subsystem = fields.contains("subsystem") ? fields.at("subsystem") : "unknown";
    record.unit = fields.contains("unit") ? fields.at("unit") : "unknown";
    if (fields.contains("quality")) {
        if (!parse_double(fields.at("quality"), record.quality) || record.quality < 0.0 || record.quality > 1.0) {
            error = "telemetry quality must be a finite number in [0,1]";
            return false;
        }
    }
    if (record.metric.empty()) {
        error = "telemetry metric is empty";
        return false;
    }
    return true;
}

TelemetryIngestStatus TelemetryIngestionPipeline::ingest(const GroundPacket& packet, std::string& error) {
    error.clear();
    ++stats_.packets_received;

    if (packet.header.type != GroundPacketType::Telemetry) {
        ++stats_.unsupported_packets;
        error = "packet is not telemetry";
        return TelemetryIngestStatus::UnsupportedPacketType;
    }

    const auto key = stream_key(packet);
    const auto previous = last_sequence_by_stream_.find(key);
    if (previous != last_sequence_by_stream_.end()) {
        if (packet.header.sequence_number == previous->second) {
            ++stats_.duplicate_packets;
            if (configuration_.reject_duplicates) {
                error = "duplicate telemetry sequence number";
                return TelemetryIngestStatus::Duplicate;
            }
        }
        if (packet.header.sequence_number < previous->second) {
            ++stats_.out_of_order_packets;
            if (configuration_.reject_out_of_order) {
                error = "out-of-order telemetry sequence number";
                return TelemetryIngestStatus::OutOfOrder;
            }
        }
    }

    TelemetryRecord record{};
    if (!parse_payload(packet, record, error)) {
        ++stats_.invalid_payloads;
        return TelemetryIngestStatus::InvalidPayload;
    }

    if (archive_.size() >= configuration_.archive_capacity) {
        ++stats_.capacity_rejections;
        error = "telemetry archive capacity reached";
        return TelemetryIngestStatus::CapacityFull;
    }

    archive_.push_back(record);
    last_sequence_by_stream_[key] = packet.header.sequence_number;
    current_state_[MetricKey{record.source_node, record.origin_node, record.metric}] = record;
    ++stats_.packets_accepted;
    return TelemetryIngestStatus::Accepted;
}

bool TelemetryIngestionPipeline::pop_record(TelemetryRecord& record) {
    if (archive_.empty()) {
        return false;
    }
    record = std::move(archive_.front());
    archive_.pop_front();
    return true;
}

bool TelemetryIngestionPipeline::get_current(const std::string& source_node, const std::string& origin_node,
                                             const std::string& metric, TelemetryRecord& record) const {
    const auto it = current_state_.find(MetricKey{source_node, origin_node, metric});
    if (it == current_state_.end()) {
        return false;
    }
    record = it->second;
    return true;
}

void TelemetryIngestionPipeline::clear_archive() noexcept {
    archive_.clear();
}

void TelemetryIngestionPipeline::reset_statistics() noexcept {
    stats_ = {};
}

} // namespace trishula
