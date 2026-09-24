#include "trishula/ground/event_ingestion.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <unordered_map>

namespace trishula {
namespace {

bool parse_u8(const std::string& text, std::uint8_t& value) {
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0' || parsed < 0L || parsed > 3L) return false;
    value = static_cast<std::uint8_t>(parsed);
    return true;
}

bool parse_bool(const std::string& text, bool& value) {
    if (text == "1" || text == "true" || text == "TRUE") { value = true; return true; }
    if (text == "0" || text == "false" || text == "FALSE") { value = false; return true; }
    return false;
}

bool parse_kind(const std::string& text, EventKind& kind) {
    if (text == "event" || text == "EVENT" || text == "mission_event") { kind = EventKind::MissionEvent; return true; }
    if (text == "fault" || text == "FAULT") { kind = EventKind::Fault; return true; }
    if (text == "recovery" || text == "RECOVERY") { kind = EventKind::Recovery; return true; }
    return false;
}

bool parse_payload_fields(const std::string& payload, std::unordered_map<std::string, std::string>& fields) {
    std::stringstream stream(payload);
    std::string token;
    while (std::getline(stream, token, '|')) {
        const auto pos = token.find('=');
        if (pos == std::string::npos) return false;
        std::string key = token.substr(0, pos);
        std::string value = token.substr(pos + 1U);
        const auto trim_local = [](std::string& text) {
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) { text.clear(); return; }
            const auto end = text.find_last_not_of(" \t\r\n");
            text = text.substr(begin, end - begin + 1U);
        };
        trim_local(key); trim_local(value);
        if (key.empty() || value.empty()) return false;
        fields[key] = value;
    }
    return !fields.empty();
}

} // namespace

std::size_t EventIngestionPipeline::StreamKeyHash::operator()(const StreamKey& key) const noexcept {
    const auto h1 = std::hash<std::string>{}(key.source_node);
    const auto h2 = std::hash<std::string>{}(key.origin_node);
    const auto h3 = std::hash<std::uint16_t>{}(key.application_id);
    return h1 ^ (h2 << 1U) ^ (h3 << 2U);
}

std::size_t EventIngestionPipeline::FaultKeyHash::operator()(const FaultKey& key) const noexcept {
    const auto h1 = std::hash<std::string>{}(key.origin_node);
    const auto h2 = std::hash<std::string>{}(key.code);
    return h1 ^ (h2 << 1U);
}

EventIngestionPipeline::EventIngestionPipeline(EventIngestionConfiguration configuration)
    : configuration_(std::move(configuration)) {
}

const EventIngestionConfiguration& EventIngestionPipeline::configuration() const noexcept { return configuration_; }
const EventIngestionStats& EventIngestionPipeline::stats() const noexcept { return stats_; }
std::size_t EventIngestionPipeline::archived_events() const noexcept { return archive_.size(); }

std::size_t EventIngestionPipeline::available_capacity() const noexcept {
    if (archive_.size() >= configuration_.archive_capacity) {
        return 0U;
    }
    return configuration_.archive_capacity - archive_.size();
}

double EventIngestionPipeline::capacity_utilization() const noexcept {
    if (configuration_.archive_capacity == 0U) {
        return 1.0;
    }
    return static_cast<double>(archive_.size()) /
           static_cast<double>(configuration_.archive_capacity);
}
std::size_t EventIngestionPipeline::active_faults() const noexcept { return active_faults_.size(); }

std::string EventIngestionPipeline::trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1U);
}

bool EventIngestionPipeline::parse_payload(const GroundPacket& packet, GroundEventRecord& event, std::string& error) {
    const std::string payload(packet.payload.begin(), packet.payload.end());
    std::unordered_map<std::string, std::string> fields;
    if (!parse_payload_fields(payload, fields)) {
        error = "event payload must contain key=value fields separated by |";
        return false;
    }

    const auto code_it = fields.find("code");
    if (code_it == fields.end() || code_it->second.empty()) { error = "event code is required"; return false; }
    event.code = trim(code_it->second);

    const auto subsystem_it = fields.find("subsystem");
    if (subsystem_it == fields.end() || subsystem_it->second.empty()) { error = "event subsystem is required"; return false; }
    event.subsystem = trim(subsystem_it->second);

    const auto message_it = fields.find("message");
    if (message_it == fields.end() || message_it->second.empty()) { error = "event message is required"; return false; }
    event.message = trim(message_it->second);

    event.kind = EventKind::MissionEvent;
    if (const auto it = fields.find("kind"); it != fields.end() && !parse_kind(it->second, event.kind)) {
        error = "invalid event kind"; return false;
    }

    event.severity = EventSeverity::Info;
    if (const auto it = fields.find("severity"); it != fields.end()) {
        std::uint8_t severity = 0U;
        if (!parse_u8(it->second, severity)) { error = "invalid event severity"; return false; }
        event.severity = static_cast<EventSeverity>(severity);
    }

    event.active = event.kind == EventKind::Fault;
    if (const auto it = fields.find("active"); it != fields.end() && !parse_bool(it->second, event.active)) {
        error = "invalid event active flag"; return false;
    }
    if (event.kind == EventKind::Recovery) event.active = false;
    return true;
}

EventIngestionPipeline::StreamKey EventIngestionPipeline::stream_key(const GroundPacket& packet) {
    return {packet.header.source_node, packet.header.origin_node, packet.header.application_id};
}

EventIngestStatus EventIngestionPipeline::ingest(const GroundPacket& packet, std::string& error) {
    error.clear();
    ++stats_.packets_received;
    if (packet.header.type != GroundPacketType::Event) {
        ++stats_.unsupported_packets;
        error = "packet is not an event packet";
        return EventIngestStatus::UnsupportedPacketType;
    }

    GroundEventRecord event{};
    if (!parse_payload(packet, event, error)) {
        ++stats_.invalid_payloads;
        return EventIngestStatus::InvalidPayload;
    }

    const StreamKey key = stream_key(packet);
    const auto sequence_it = last_sequence_by_stream_.find(key);
    if (sequence_it != last_sequence_by_stream_.end()) {
        if (packet.header.sequence_number == sequence_it->second) {
            ++stats_.duplicate_packets;
            if (configuration_.reject_duplicates) return EventIngestStatus::Duplicate;
        }
        if (packet.header.sequence_number < sequence_it->second) {
            ++stats_.out_of_order_packets;
            if (configuration_.reject_out_of_order) return EventIngestStatus::OutOfOrder;
        }
    }

    if (archive_.size() >= configuration_.archive_capacity) {
        ++stats_.capacity_rejections;
        return EventIngestStatus::CapacityFull;
    }

    event.source_node = packet.header.source_node;
    event.origin_node = packet.header.origin_node;
    event.destination_node = packet.header.destination_node;
    event.application_id = packet.header.application_id;
    event.mission_timestamp_ns = packet.header.mission_timestamp_ns;
    event.sequence_number = packet.header.sequence_number;

    archive_.push_back(event);
    last_sequence_by_stream_[key] = packet.header.sequence_number;
    ++stats_.packets_accepted;
    if (event.kind == EventKind::Fault && event.active) {
        active_faults_[FaultKey{event.origin_node, event.code}] = event;
        ++stats_.fault_events;
    } else if (event.kind == EventKind::Recovery) {
        active_faults_.erase(FaultKey{event.origin_node, event.code});
        ++stats_.recovery_events;
    }
    return EventIngestStatus::Accepted;
}

bool EventIngestionPipeline::pop_event(GroundEventRecord& event) {
    if (archive_.empty()) return false;
    event = std::move(archive_.front());
    archive_.pop_front();
    return true;
}

bool EventIngestionPipeline::get_active_fault(const std::string& origin_node, const std::string& code,
                                              GroundEventRecord& event) const {
    const auto it = active_faults_.find(FaultKey{origin_node, code});
    if (it == active_faults_.end()) return false;
    event = it->second;
    return true;
}

bool EventIngestionPipeline::clear_fault(const std::string& origin_node, const std::string& code) {
    return active_faults_.erase(FaultKey{origin_node, code}) != 0U;
}

void EventIngestionPipeline::clear_archive() noexcept { archive_.clear(); }
void EventIngestionPipeline::reset_statistics() noexcept { stats_ = {}; }

} // namespace trishula
