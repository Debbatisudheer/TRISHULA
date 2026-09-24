#include "trishula/ground/command_ingestion.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

namespace trishula {
namespace {

bool parse_payload_fields(const std::string& payload, std::unordered_map<std::string, std::string>& fields) {
    std::stringstream stream(payload);
    std::string token;
    while (std::getline(stream, token, '|')) {
        const auto separator = token.find('=');
        if (separator == std::string::npos) return false;
        std::string key = token.substr(0U, separator);
        std::string value = token.substr(separator + 1U);
        const auto trim_local = [](std::string& text) {
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) { text.clear(); return; }
            const auto end = text.find_last_not_of(" \t\r\n");
            text = text.substr(begin, end - begin + 1U);
        };
        trim_local(key);
        trim_local(value);
        if (key.empty()) return false;
        fields[key] = value;
    }
    return !fields.empty();
}

} // namespace

std::size_t CommandIngestionPipeline::StreamKeyHash::operator()(const StreamKey& key) const noexcept {
    const auto h1 = std::hash<std::string>{}(key.source_node);
    const auto h2 = std::hash<std::string>{}(key.destination_node);
    const auto h3 = std::hash<std::uint16_t>{}(key.application_id);
    return h1 ^ (h2 << 1U) ^ (h3 << 2U);
}

CommandIngestionPipeline::CommandIngestionPipeline(CommandIngestionConfiguration configuration)
    : configuration_(std::move(configuration)) {
    command_ids_.reserve(configuration_.archive_capacity * 2U);
}

const CommandIngestionConfiguration& CommandIngestionPipeline::configuration() const noexcept {
    return configuration_;
}

const CommandIngestionStats& CommandIngestionPipeline::stats() const noexcept {
    return stats_;
}

std::size_t CommandIngestionPipeline::archived_commands() const noexcept {
    return archive_.size();
}

std::size_t CommandIngestionPipeline::available_capacity() const noexcept {
    if (archive_.size() >= configuration_.archive_capacity) {
        return 0U;
    }
    return configuration_.archive_capacity - archive_.size();
}

double CommandIngestionPipeline::capacity_utilization() const noexcept {
    if (configuration_.archive_capacity == 0U) {
        return 1.0;
    }
    return static_cast<double>(archive_.size()) /
           static_cast<double>(configuration_.archive_capacity);
}

std::string CommandIngestionPipeline::trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1U);
}

bool CommandIngestionPipeline::valid_identifier(const std::string& value) noexcept {
    if (value.empty()) return false;
    for (const unsigned char ch : value) {
        if (!(std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.')) return false;
    }
    return true;
}

std::string CommandIngestionPipeline::uppercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

CommandIngestionPipeline::StreamKey CommandIngestionPipeline::stream_key(const GroundPacket& packet) {
    return {packet.header.source_node, packet.header.destination_node, packet.header.application_id};
}

bool CommandIngestionPipeline::parse_payload(const GroundPacket& packet, GroundCommandRecord& command, std::string& error) const {
    error.clear();
    command = {};
    command.source_node = packet.header.source_node;
    command.destination_node = packet.header.destination_node;
    command.priority = packet.header.priority;
    command.application_id = packet.header.application_id;
    command.mission_timestamp_ns = packet.header.mission_timestamp_ns;
    command.sequence_number = packet.header.sequence_number;

    const std::string payload(packet.payload.begin(), packet.payload.end());
    if (payload.empty()) {
        error = "command payload is empty";
        return false;
    }

    std::unordered_map<std::string, std::string> fields;
    if (!parse_payload_fields(payload, fields)) {
        error = "command payload must contain key=value fields separated by |";
        return false;
    }

    const auto command_id_it = fields.find("command_id");
    const auto target_it = fields.find("target");
    const auto opcode_it = fields.find("opcode");
    if (command_id_it == fields.end() || target_it == fields.end() || opcode_it == fields.end()) {
        error = "command requires command_id=, target= and opcode=";
        return false;
    }

    command.command_id = trim(command_id_it->second);
    command.target = uppercase(trim(target_it->second));
    command.opcode = uppercase(trim(opcode_it->second));
    if (!valid_identifier(command.command_id)) {
        error = "command_id contains invalid characters";
        return false;
    }
    if (!valid_identifier(command.target)) {
        error = "command target contains invalid characters";
        return false;
    }
    if (!valid_identifier(command.opcode)) {
        error = "command opcode contains invalid characters";
        return false;
    }

    if (const auto parameters_it = fields.find("parameters"); parameters_it != fields.end()) {
        command.parameters = trim(parameters_it->second);
    }
    if (configuration_.require_non_empty_parameters && command.parameters.empty()) {
        error = "command parameters are required";
        return false;
    }
    return true;
}

CommandIngestStatus CommandIngestionPipeline::ingest(const GroundPacket& packet, std::string& error) {
    error.clear();
    ++stats_.packets_received;

    if (packet.header.type != GroundPacketType::Command) {
        ++stats_.unsupported_packets;
        error = "packet is not a command";
        return CommandIngestStatus::UnsupportedPacketType;
    }

    GroundCommandRecord command{};
    if (!parse_payload(packet, command, error)) {
        ++stats_.invalid_payloads;
        return CommandIngestStatus::InvalidPayload;
    }

    if (!configuration_.allowed_targets.empty() && !configuration_.allowed_targets.contains(command.target)) {
        ++stats_.invalid_targets;
        error = "command target is not allowed: " + command.target;
        return CommandIngestStatus::InvalidTarget;
    }

    if (!configuration_.allowed_opcodes.empty() && !configuration_.allowed_opcodes.contains(command.opcode)) {
        ++stats_.unknown_opcodes;
        error = "command opcode is not allowed: " + command.opcode;
        return CommandIngestStatus::UnknownOpcode;
    }

    if (command_ids_.contains(command.command_id)) {
        ++stats_.duplicate_packets;
        if (configuration_.reject_duplicates) {
            error = "duplicate command_id: " + command.command_id;
            return CommandIngestStatus::Duplicate;
        }
    }

    const auto key = stream_key(packet);
    const auto previous = last_sequence_by_stream_.find(key);
    if (previous != last_sequence_by_stream_.end() && packet.header.sequence_number < previous->second) {
        ++stats_.out_of_order_packets;
        if (configuration_.reject_out_of_order) {
            error = "out-of-order command sequence number";
            return CommandIngestStatus::OutOfOrder;
        }
    }

    if (archive_.size() >= configuration_.archive_capacity) {
        ++stats_.capacity_rejections;
        error = "command archive capacity reached";
        return CommandIngestStatus::CapacityFull;
    }

    archive_.push_back(command);
    command_ids_.insert(command.command_id);
    last_sequence_by_stream_[key] = packet.header.sequence_number;
    ++stats_.packets_accepted;
    return CommandIngestStatus::Accepted;
}

bool CommandIngestionPipeline::pop_command(GroundCommandRecord& command) {
    if (archive_.empty()) return false;
    command = std::move(archive_.front());
    archive_.pop_front();
    return true;
}

bool CommandIngestionPipeline::contains_command_id(const std::string& command_id) const noexcept {
    return command_ids_.contains(command_id);
}

void CommandIngestionPipeline::clear_archive() noexcept {
    archive_.clear();
}

void CommandIngestionPipeline::reset_statistics() noexcept {
    stats_ = {};
}

} // namespace trishula
