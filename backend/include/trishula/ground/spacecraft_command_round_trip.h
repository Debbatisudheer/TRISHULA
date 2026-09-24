#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "trishula/core/simulation_engine.h"
#include "trishula/ground/command_ingestion.h"
#include "trishula/ground/space_link.h"
#include "trishula/ground/spacecraft_command_link.h"
#include "trishula/ground/telemetry_ingestion.h"

namespace trishula {

enum class CommandRoundTripStatus {
    Applied,
    UplinkEncodingFailed,
    SpacecraftRejected,
    DownlinkEncodingFailed,
    GroundReceptionFailed,
    TelemetryIngestionFailed
};

struct CommandRoundTripResult {
    CommandRoundTripStatus status{CommandRoundTripStatus::UplinkEncodingFailed};
    std::string message{};
    GroundPacket uplink_packet{};
    SpaceLinkFrame uplink_frame{};
    SpacecraftCommandResult spacecraft_result{};
    GroundPacket acknowledgement_packet{};
    GroundPacket telemetry_packet{};
    SpaceLinkFrame acknowledgement_frame{};
    SpaceLinkFrame telemetry_frame{};
    std::size_t ground_packets_received{0U};
    std::size_t telemetry_records_ingested{0U};
};

struct CommandRoundTripConfiguration {
    std::string ground_node{"GROUND-OPS-01"};
    std::string spacecraft_node{"VIKRAM"};
    std::uint16_t application_id{502U};
    std::uint32_t uplink_frame_sequence_start{500000U};
    std::uint32_t downlink_frame_sequence_start{600000U};
    std::uint32_t acknowledgement_sequence_start{700000U};
    std::uint32_t telemetry_sequence_start{800000U};
};

class GroundSpacecraftCommandRoundTrip {
public:
    explicit GroundSpacecraftCommandRoundTrip(CommandRoundTripConfiguration configuration = {});

    [[nodiscard]] const CommandRoundTripConfiguration& configuration() const noexcept;
    [[nodiscard]] const GroundStation& ground_station() const noexcept;
    [[nodiscard]] const TelemetryIngestionPipeline& telemetry_pipeline() const noexcept;

    CommandRoundTripResult execute(const GroundCommandRecord& command, SimulationEngine& simulation);
    void reset() noexcept;

private:
    GroundPacket make_uplink_packet(const GroundCommandRecord& command) const;
    GroundPacket make_ack_packet(const GroundCommandRecord& command,
                                 const SpacecraftCommandResult& result) const;
    GroundPacket make_telemetry_packet(const GroundCommandRecord& command,
                                       const SpacecraftCommandResult& result,
                                       const char* metric,
                                       double value) const;
    static std::string status_name(SpacecraftCommandStatus status);

    CommandRoundTripConfiguration configuration_{};
    GroundStation ground_station_{};
    TelemetryIngestionPipeline telemetry_pipeline_{};
    std::uint32_t next_uplink_frame_sequence_{0U};
    std::uint32_t next_downlink_frame_sequence_{0U};
    std::uint32_t next_ack_sequence_{0U};
    std::uint32_t next_telemetry_sequence_{0U};
};

} // namespace trishula
