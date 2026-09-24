#include "trishula/ground/closed_loop_feedback.h"

#include <sstream>

namespace trishula {
namespace {

std::string make_event_payload(const GroundCommandRecord& command,
                               const VehicleAdapterResult& physical,
                               const int severity) {
    std::ostringstream out;
    out << "kind=event"
        << "|severity=" << severity
        << "|code=COMMAND_VEHICLE_FEEDBACK"
        << "|subsystem=vehicle_command_adapter"
        << "|active=false"
        << "|message=command_id=" << command.command_id
        << ",opcode=" << command.opcode
        << ",target=" << physical.snapshot.target
        << ",vehicle_status=" << ([&]() {
            switch (physical.status) {
            case VehicleAdapterStatus::Applied: return "APPLIED";
            case VehicleAdapterStatus::UnsupportedTarget: return "UNSUPPORTED_TARGET";
            case VehicleAdapterStatus::UnsupportedOpcode: return "UNSUPPORTED_OPCODE";
            case VehicleAdapterStatus::PreconditionsFailed: return "PRECONDITIONS_FAILED";
            case VehicleAdapterStatus::InvalidParameters: return "INVALID_PARAMETERS";
            case VehicleAdapterStatus::PhysicalFault: return "PHYSICAL_FAULT";
            }
            return "UNKNOWN";
        })()
        << ",details=" << physical.message;
    return out.str();
}

} // namespace

GroundClosedLoopFeedbackEngine::GroundClosedLoopFeedbackEngine(ClosedLoopFeedbackConfiguration configuration)
    : configuration_(configuration),
      execution_engine_(),
      vehicle_adapter_(),
      event_pipeline_(),
      telemetry_pipeline_(),
      next_event_sequence_(configuration_.event_sequence_start),
      next_telemetry_sequence_(configuration_.telemetry_sequence_start) {}

const ClosedLoopFeedbackConfiguration& GroundClosedLoopFeedbackEngine::configuration() const noexcept {
    return configuration_;
}

const GroundCommandExecutionEngine& GroundClosedLoopFeedbackEngine::execution_engine() const noexcept {
    return execution_engine_;
}

const VehicleCommandAdapter& GroundClosedLoopFeedbackEngine::vehicle_adapter() const noexcept {
    return vehicle_adapter_;
}

const EventIngestionPipeline& GroundClosedLoopFeedbackEngine::event_pipeline() const noexcept {
    return event_pipeline_;
}

const TelemetryIngestionPipeline& GroundClosedLoopFeedbackEngine::telemetry_pipeline() const noexcept {
    return telemetry_pipeline_;
}

int GroundClosedLoopFeedbackEngine::event_severity_for(const VehicleAdapterStatus status) noexcept {
    switch (status) {
    case VehicleAdapterStatus::Applied:
        return 1;
    case VehicleAdapterStatus::PreconditionsFailed:
    case VehicleAdapterStatus::InvalidParameters:
    case VehicleAdapterStatus::UnsupportedOpcode:
        return 2;
    case VehicleAdapterStatus::PhysicalFault:
    case VehicleAdapterStatus::UnsupportedTarget:
        return 3;
    }
    return 3;
}

const char* GroundClosedLoopFeedbackEngine::vehicle_status_name(const VehicleAdapterStatus status) noexcept {
    switch (status) {
    case VehicleAdapterStatus::Applied: return "APPLIED";
    case VehicleAdapterStatus::UnsupportedTarget: return "UNSUPPORTED_TARGET";
    case VehicleAdapterStatus::UnsupportedOpcode: return "UNSUPPORTED_OPCODE";
    case VehicleAdapterStatus::PreconditionsFailed: return "PRECONDITIONS_FAILED";
    case VehicleAdapterStatus::InvalidParameters: return "INVALID_PARAMETERS";
    case VehicleAdapterStatus::PhysicalFault: return "PHYSICAL_FAULT";
    }
    return "UNKNOWN";
}

GroundPacket GroundClosedLoopFeedbackEngine::make_event_packet(const GroundCommandRecord& command,
                                                               const VehicleAdapterResult& physical,
                                                               const std::uint32_t sequence) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Event;
    packet.header.priority = physical.status == VehicleAdapterStatus::Applied
        ? GroundPacketPriority::High : GroundPacketPriority::Critical;
    packet.header.source_node = physical.snapshot.target;
    packet.header.origin_node = physical.snapshot.target;
    packet.header.destination_node = "GROUND";
    packet.header.application_id = configuration_.application_id;
    packet.header.mission_timestamp_ns = command.mission_timestamp_ns;
    packet.header.sequence_number = sequence;
    const auto payload = make_event_payload(command, physical, event_severity_for(physical.status));
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

GroundPacket GroundClosedLoopFeedbackEngine::make_telemetry_packet(const GroundCommandRecord& command,
                                                                    const VehicleAdapterResult& physical,
                                                                    const std::string& metric,
                                                                    const double value,
                                                                    const std::uint32_t sequence) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Telemetry;
    packet.header.priority = GroundPacketPriority::Normal;
    packet.header.source_node = physical.snapshot.target;
    packet.header.origin_node = physical.snapshot.target;
    packet.header.destination_node = "GROUND";
    packet.header.application_id = configuration_.application_id;
    packet.header.mission_timestamp_ns = command.mission_timestamp_ns;
    packet.header.sequence_number = sequence;
    std::ostringstream payload;
    payload << "metric=" << metric
            << "|value=" << value
            << "|unit=" << (metric == "rover_battery_soc" ? "fraction" :
                               metric == "rover_x_m" || metric == "rover_y_m" ? "m" :
                               metric == "rover_heading_rad" ? "rad" :
                               metric == "rover_speed_m_s" ? "m/s" : "count")
            << "|subsystem=rover"
            << "|quality=1.0";
    const auto text = payload.str();
    packet.payload.assign(text.begin(), text.end());
    return packet;
}

ClosedLoopFeedbackResult GroundClosedLoopFeedbackEngine::process(const GroundCommandRecord& command) {
    ClosedLoopFeedbackResult result{};
    result.execution = execution_engine_.execute(command);
    if (result.execution.status != CommandExecutionStatus::Executed) {
        result.status = ClosedLoopStatus::LogicalExecutionFailed;
        result.message = result.execution.message;
        return result;
    }

    result.physical = vehicle_adapter_.apply(command, result.execution);
    if (result.physical.status != VehicleAdapterStatus::Applied) {
        result.status = ClosedLoopStatus::PhysicalApplicationFailed;
        result.message = result.physical.message;
    }

    std::string error;
    const auto event_packet = make_event_packet(command, result.physical, next_event_sequence_++);
    if (event_pipeline_.ingest(event_packet, error) != EventIngestStatus::Accepted) {
        result.status = ClosedLoopStatus::FeedbackIngestionFailed;
        result.message = "event feedback ingestion failed: " + error;
        return result;
    }
    result.events_ingested = 1U;

    const double values[] = {
        result.physical.snapshot.x_m,
        result.physical.snapshot.y_m,
        result.physical.snapshot.heading_rad,
        result.physical.snapshot.speed_m_s,
        result.physical.snapshot.battery_soc,
        result.physical.snapshot.physical_steps == 0U ? 0.0 : static_cast<double>(result.physical.snapshot.physical_steps)
    };
    const char* names[] = {
        "rover_x_m",
        "rover_y_m",
        "rover_heading_rad",
        "rover_speed_m_s",
        "rover_battery_soc",
        "rover_physical_steps"
    };

    for (std::size_t index = 0U; index < 6U; ++index) {
        const auto telemetry_packet = make_telemetry_packet(command, result.physical, names[index], values[index], next_telemetry_sequence_++);
        if (telemetry_pipeline_.ingest(telemetry_packet, error) != TelemetryIngestStatus::Accepted) {
            result.status = ClosedLoopStatus::FeedbackIngestionFailed;
            result.message = "telemetry feedback ingestion failed: " + error;
            return result;
        }
        ++result.telemetry_records_ingested;
    }

    if (result.physical.status == VehicleAdapterStatus::Applied) {
        result.status = ClosedLoopStatus::Applied;
        result.message = "command executed, physically applied, and feedback returned to ground";
    } else {
        result.message = "command executed but physical adapter did not apply it fully; feedback returned to ground";
    }
    return result;
}

void GroundClosedLoopFeedbackEngine::reset() noexcept {
    execution_engine_.reset();
    vehicle_adapter_.reset();
    event_pipeline_.clear_archive();
    event_pipeline_.reset_statistics();
    telemetry_pipeline_.clear_archive();
    telemetry_pipeline_.reset_statistics();
    next_event_sequence_ = configuration_.event_sequence_start;
    next_telemetry_sequence_ = configuration_.telemetry_sequence_start;
}

} // namespace trishula
