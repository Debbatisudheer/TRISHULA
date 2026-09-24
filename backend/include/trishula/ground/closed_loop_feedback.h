#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "trishula/ground/command_execution.h"
#include "trishula/ground/event_ingestion.h"
#include "trishula/ground/telemetry_ingestion.h"
#include "trishula/ground/vehicle_command_adapter.h"

namespace trishula {

enum class ClosedLoopStatus {
    Applied,
    LogicalExecutionFailed,
    PhysicalApplicationFailed,
    FeedbackIngestionFailed
};

struct ClosedLoopFeedbackResult {
    ClosedLoopStatus status{ClosedLoopStatus::LogicalExecutionFailed};
    std::string message{};
    CommandExecutionResult execution{};
    VehicleAdapterResult physical{};
    std::size_t telemetry_records_ingested{0U};
    std::size_t events_ingested{0U};
};

struct ClosedLoopFeedbackConfiguration {
    std::uint32_t event_sequence_start{300000U};
    std::uint32_t telemetry_sequence_start{400000U};
    std::uint16_t application_id{201U};
};

class GroundClosedLoopFeedbackEngine {
public:
    explicit GroundClosedLoopFeedbackEngine(ClosedLoopFeedbackConfiguration configuration = {});

    [[nodiscard]] const ClosedLoopFeedbackConfiguration& configuration() const noexcept;
    [[nodiscard]] const GroundCommandExecutionEngine& execution_engine() const noexcept;
    [[nodiscard]] const VehicleCommandAdapter& vehicle_adapter() const noexcept;
    [[nodiscard]] const EventIngestionPipeline& event_pipeline() const noexcept;
    [[nodiscard]] const TelemetryIngestionPipeline& telemetry_pipeline() const noexcept;

    ClosedLoopFeedbackResult process(const GroundCommandRecord& command);
    void reset() noexcept;

private:
    GroundPacket make_event_packet(const GroundCommandRecord& command,
                                   const VehicleAdapterResult& physical,
                                   std::uint32_t sequence);
    GroundPacket make_telemetry_packet(const GroundCommandRecord& command,
                                       const VehicleAdapterResult& physical,
                                       const std::string& metric,
                                       double value,
                                       std::uint32_t sequence);
    static int event_severity_for(VehicleAdapterStatus status) noexcept;
    static const char* vehicle_status_name(VehicleAdapterStatus status) noexcept;

    ClosedLoopFeedbackConfiguration configuration_{};
    GroundCommandExecutionEngine execution_engine_{};
    VehicleCommandAdapter vehicle_adapter_{};
    EventIngestionPipeline event_pipeline_{};
    TelemetryIngestionPipeline telemetry_pipeline_{};
    std::uint32_t next_event_sequence_{0U};
    std::uint32_t next_telemetry_sequence_{0U};
};

} // namespace trishula
