#include "trishula/ground/spacecraft_command_round_trip.h"

#include <sstream>
#include <utility>

namespace trishula {
namespace {

std::string hex_encode(const std::string& input) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(input.size() * 2U);
    for (const unsigned char ch : input) {
        out.push_back(hex[ch >> 4U]);
        out.push_back(hex[ch & 0x0FU]);
    }
    return out;
}

bool hex_decode(const std::string& input, std::string& output) {
    if ((input.size() % 2U) != 0U) return false;
    output.clear();
    output.reserve(input.size() / 2U);
    const auto nibble = [](const char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0U; i < input.size(); i += 2U) {
        const int hi = nibble(input[i]);
        const int lo = nibble(input[i + 1U]);
        if (hi < 0 || lo < 0) return false;
        output.push_back(static_cast<char>((hi << 4) | lo));
    }
    return true;
}

} // namespace

GroundSpacecraftCommandRoundTrip::GroundSpacecraftCommandRoundTrip(CommandRoundTripConfiguration configuration)
    : configuration_(std::move(configuration)),
      ground_station_(GroundStationConfiguration{}),
      telemetry_pipeline_(),
      next_uplink_frame_sequence_(configuration_.uplink_frame_sequence_start),
      next_downlink_frame_sequence_(configuration_.downlink_frame_sequence_start),
      next_ack_sequence_(configuration_.acknowledgement_sequence_start),
      next_telemetry_sequence_(configuration_.telemetry_sequence_start) {}

const CommandRoundTripConfiguration& GroundSpacecraftCommandRoundTrip::configuration() const noexcept {
    return configuration_;
}

const GroundStation& GroundSpacecraftCommandRoundTrip::ground_station() const noexcept {
    return ground_station_;
}

const TelemetryIngestionPipeline& GroundSpacecraftCommandRoundTrip::telemetry_pipeline() const noexcept {
    return telemetry_pipeline_;
}

GroundPacket GroundSpacecraftCommandRoundTrip::make_uplink_packet(const GroundCommandRecord& command) const {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Command;
    packet.header.priority = command.priority;
    packet.header.source_node = configuration_.ground_node;
    packet.header.origin_node = configuration_.ground_node;
    packet.header.destination_node = configuration_.spacecraft_node;
    packet.header.application_id = configuration_.application_id;
    packet.header.mission_timestamp_ns = command.mission_timestamp_ns;
    packet.header.sequence_number = command.sequence_number;

    std::ostringstream payload;
    payload << "command_id=" << command.command_id
            << "|target=" << command.target
            << "|opcode=" << command.opcode
            << "|parameters_hex=" << hex_encode(command.parameters);
    const auto text = payload.str();
    packet.payload.assign(text.begin(), text.end());
    return packet;
}

std::string GroundSpacecraftCommandRoundTrip::status_name(const SpacecraftCommandStatus status) {
    switch (status) {
    case SpacecraftCommandStatus::Applied: return "APPLIED";
    case SpacecraftCommandStatus::InvalidTarget: return "INVALID_TARGET";
    case SpacecraftCommandStatus::InvalidOpcode: return "INVALID_OPCODE";
    case SpacecraftCommandStatus::InvalidParameters: return "INVALID_PARAMETERS";
    case SpacecraftCommandStatus::PreconditionsFailed: return "PRECONDITIONS_FAILED";
    case SpacecraftCommandStatus::PhysicalFault: return "PHYSICAL_FAULT";
    }
    return "UNKNOWN";
}

GroundPacket GroundSpacecraftCommandRoundTrip::make_ack_packet(
    const GroundCommandRecord& command,
    const SpacecraftCommandResult& result) const {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Event;
    packet.header.priority = result.status == SpacecraftCommandStatus::Applied
        ? GroundPacketPriority::High : GroundPacketPriority::Critical;
    packet.header.source_node = configuration_.spacecraft_node;
    packet.header.origin_node = configuration_.spacecraft_node;
    packet.header.destination_node = configuration_.ground_node;
    packet.header.application_id = configuration_.application_id;
    packet.header.mission_timestamp_ns = command.mission_timestamp_ns;
    packet.header.sequence_number = next_ack_sequence_;

    std::ostringstream payload;
    payload << "event=COMMAND_ACK"
            << "|command_id=" << command.command_id
            << "|sequence=" << command.sequence_number
            << "|status=" << status_name(result.status)
            << "|propagated_seconds=" << result.propagated_seconds
            << "|message=" << result.message;
    const auto text = payload.str();
    packet.payload.assign(text.begin(), text.end());
    return packet;
}

GroundPacket GroundSpacecraftCommandRoundTrip::make_telemetry_packet(
    const GroundCommandRecord& command,
    const SpacecraftCommandResult& result,
    const char* metric,
    const double value) const {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Telemetry;
    packet.header.priority = GroundPacketPriority::Normal;
    packet.header.source_node = configuration_.spacecraft_node;
    packet.header.origin_node = configuration_.spacecraft_node;
    packet.header.destination_node = configuration_.ground_node;
    packet.header.application_id = configuration_.application_id;
    packet.header.mission_timestamp_ns = command.mission_timestamp_ns;
    packet.header.sequence_number = next_telemetry_sequence_;

    std::ostringstream payload;
    payload << "metric=" << metric
            << "|value=" << value
            << "|unit=" << (std::string(metric) == "spacecraft_time_s" ? "s" :
                               std::string(metric) == "spacecraft_speed_m_s" ? "m/s" :
                               std::string(metric) == "spacecraft_mass_kg" ? "kg" : "bool")
            << "|subsystem=spacecraft"
            << "|quality=1.0"
            << "|command_status=" << status_name(result.status);
    const auto text = payload.str();
    packet.payload.assign(text.begin(), text.end());
    return packet;
}

CommandRoundTripResult GroundSpacecraftCommandRoundTrip::execute(
    const GroundCommandRecord& command, SimulationEngine& simulation) {
    CommandRoundTripResult result{};
    result.uplink_packet = make_uplink_packet(command);

    std::string error;
    if (!encode_frame(result.uplink_packet, next_uplink_frame_sequence_++, result.uplink_frame, error)) {
        result.status = CommandRoundTripStatus::UplinkEncodingFailed;
        result.message = error;
        return result;
    }

    GroundPacket decoded_uplink{};
    if (!decode_packet(result.uplink_frame.packet_bytes, decoded_uplink, error)) {
        result.status = CommandRoundTripStatus::UplinkEncodingFailed;
        result.message = "uplink packet decode failed: " + error;
        return result;
    }

    CommandIngestionPipeline command_ingestion{};
    GroundCommandRecord decoded_command{};
    if (command_ingestion.ingest(decoded_uplink, error) != CommandIngestStatus::Accepted ||
        !command_ingestion.pop_command(decoded_command)) {
        result.status = CommandRoundTripStatus::SpacecraftRejected;
        result.message = "uplink command ingestion failed: " + error;
        return result;
    }

    const std::string decoded_payload(decoded_uplink.payload.begin(), decoded_uplink.payload.end());
    const auto marker = decoded_payload.find("|parameters_hex=");
    if (marker != std::string::npos) {
        const auto encoded = decoded_payload.substr(marker + 16U);
        if (!hex_decode(encoded, decoded_command.parameters)) {
            result.status = CommandRoundTripStatus::SpacecraftRejected;
            result.message = "uplink parameter encoding is invalid";
            return result;
        }
    }

    GroundSpacecraftCommandLink spacecraft_link{};
    result.spacecraft_result = spacecraft_link.apply(decoded_command, simulation);
    result.acknowledgement_packet = make_ack_packet(decoded_command, result.spacecraft_result);

    if (!encode_frame(result.acknowledgement_packet, next_downlink_frame_sequence_++, result.acknowledgement_frame, error)) {
        result.status = CommandRoundTripStatus::DownlinkEncodingFailed;
        result.message = error;
        return result;
    }

    if (ground_station_.ingest_frame(result.acknowledgement_frame, error) != GroundIngestStatus::Accepted) {
        result.status = CommandRoundTripStatus::GroundReceptionFailed;
        result.message = "acknowledgement reception failed: " + error;
        return result;
    }
    ++result.ground_packets_received;

    const auto& state = simulation.spacecraft().state();
    const double values[] = {state.time_seconds, state.velocity_m_per_s.magnitude(), state.mass_kg};
    const char* metrics[] = {"spacecraft_time_s", "spacecraft_speed_m_s", "spacecraft_mass_kg"};
    const std::size_t count = result.spacecraft_result.status == SpacecraftCommandStatus::Applied ? 3U : 0U;
    for (std::size_t i = 0U; i < count; ++i) {
        auto telemetry = make_telemetry_packet(decoded_command, result.spacecraft_result, metrics[i], values[i]);
        if (!encode_frame(telemetry, next_downlink_frame_sequence_++, result.telemetry_frame, error)) {
            result.status = CommandRoundTripStatus::DownlinkEncodingFailed;
            result.message = error;
            return result;
        }
        if (ground_station_.ingest_frame(result.telemetry_frame, error) != GroundIngestStatus::Accepted) {
            result.status = CommandRoundTripStatus::GroundReceptionFailed;
            result.message = "telemetry reception failed: " + error;
            return result;
        }
        ++result.ground_packets_received;
        result.telemetry_packet = std::move(telemetry);
        ++next_telemetry_sequence_;
        if (telemetry_pipeline_.ingest(result.telemetry_packet, error) != TelemetryIngestStatus::Accepted) {
            result.status = CommandRoundTripStatus::TelemetryIngestionFailed;
            result.message = "telemetry ingestion failed: " + error;
            return result;
        }
        ++result.telemetry_records_ingested;
    }

    result.status = result.spacecraft_result.status == SpacecraftCommandStatus::Applied
        ? CommandRoundTripStatus::Applied : CommandRoundTripStatus::SpacecraftRejected;
    result.message = result.spacecraft_result.message;
    return result;
}

void GroundSpacecraftCommandRoundTrip::reset() noexcept {
    ground_station_.clear_receive_queue();
    ground_station_.reset_statistics();
    telemetry_pipeline_.clear_archive();
    telemetry_pipeline_.reset_statistics();
    next_uplink_frame_sequence_ = configuration_.uplink_frame_sequence_start;
    next_downlink_frame_sequence_ = configuration_.downlink_frame_sequence_start;
    next_ack_sequence_ = configuration_.acknowledgement_sequence_start;
    next_telemetry_sequence_ = configuration_.telemetry_sequence_start;
}

} // namespace trishula
