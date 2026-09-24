#include "trishula/mission/physical_mission_telemetry_loop.h"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace trishula {
namespace {
std::string number(const double value) {
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}
}

PhysicalMissionTelemetryLoop::PhysicalMissionTelemetryLoop(PhysicalMissionTelemetryLoopConfiguration configuration)
    : configuration_(std::move(configuration)), mission_(configuration_.mission),
      ground_station_(GroundStationConfiguration{configuration_.destination_node, 4096U, true, false}),
      telemetry_(TelemetryIngestionConfiguration{8192U, true, false}) {
    if (configuration_.telemetry_metrics_per_tick != 11U) {
        throw std::invalid_argument("V0.9.39 requires the complete 11-metric physical telemetry set");
    }
    reset();
}

void PhysicalMissionTelemetryLoop::reset() {
    mission_.reset();
    ground_station_.clear_receive_queue();
    ground_station_.reset_statistics();
    telemetry_.clear_archive();
    telemetry_.reset_statistics();
    stats_ = {};
    packet_sequence_ = 0U;
    frame_sequence_ = 0U;
}

std::vector<PhysicalMissionTelemetryLoop::TelemetryMetric> PhysicalMissionTelemetryLoop::build_metrics() const {
    const auto& s = mission_.snapshot();
    return {
        {"position_x_m", "navigation", "m", s.position_x_m},
        {"position_y_m", "navigation", "m", s.position_y_m},
        {"position_z_m", "navigation", "m", s.position_z_m},
        {"velocity_x_m_per_s", "navigation", "m/s", s.velocity_x_m_per_s},
        {"velocity_y_m_per_s", "navigation", "m/s", s.velocity_y_m_per_s},
        {"velocity_z_m_per_s", "navigation", "m/s", s.velocity_z_m_per_s},
        {"altitude_m", "navigation", "m", s.altitude_m},
        {"speed_m_per_s", "navigation", "m/s", s.speed_m_per_s},
        {"distance_to_moon_m", "navigation", "m", s.distance_to_moon_m},
        {"moon_x_m", "ephemeris", "m", s.moon_x_m},
        {"moon_y_m", "ephemeris", "m", s.moon_y_m},
    };
}

void PhysicalMissionTelemetryLoop::publish_metric(const TelemetryMetric& metric) {
    if (!std::isfinite(metric.value)) {
        throw std::runtime_error(std::string("non-finite physical telemetry metric: ") + metric.name);
    }

    GroundPacket packet{};
    packet.header.version = 1U;
    packet.header.type = GroundPacketType::Telemetry;
    packet.header.priority = GroundPacketPriority::Normal;
    packet.header.source_node = configuration_.source_node;
    packet.header.origin_node = configuration_.origin_node;
    packet.header.destination_node = configuration_.destination_node;
    packet.header.application_id = configuration_.application_id;
    packet.header.mission_timestamp_ns = static_cast<std::uint64_t>(mission_.snapshot().mission_time_seconds * 1.0e9);
    packet.header.sequence_number = ++packet_sequence_;

    const std::string payload = "metric=" + std::string(metric.name) + "|value=" + number(metric.value) +
                                "|unit=" + metric.unit + "|subsystem=" + metric.subsystem + "|quality=1";
    packet.payload.assign(payload.begin(), payload.end());

    SpaceLinkFrame frame{};
    std::string error;
    if (!encode_frame(packet, ++frame_sequence_, frame, error)) {
        throw std::runtime_error("telemetry frame encoding failed: " + error);
    }
    ++stats_.packets_encoded;

    const auto status = ground_station_.ingest_frame(frame, error);
    if (status != GroundIngestStatus::Accepted) {
        ++stats_.rejected_packets;
        throw std::runtime_error("ground station rejected physical telemetry: " + error);
    }
    ++stats_.packets_received;

    GroundPacket received{};
    if (!ground_station_.pop_packet(received)) {
        ++stats_.rejected_packets;
        throw std::runtime_error("ground station did not expose accepted telemetry packet");
    }

    const auto ingest_status = telemetry_.ingest(received, error);
    if (ingest_status != TelemetryIngestStatus::Accepted) {
        ++stats_.rejected_packets;
        throw std::runtime_error("telemetry pipeline rejected physical telemetry: " + error);
    }
    ++stats_.telemetry_records_accepted;
}

void PhysicalMissionTelemetryLoop::step() {
    mission_.step();
    ++stats_.simulation_ticks;
    for (const auto& metric : build_metrics()) {
        publish_metric(metric);
    }
}

void PhysicalMissionTelemetryLoop::run(const std::size_t ticks) {
    for (std::size_t i = 0U; i < ticks; ++i) {
        step();
    }
}

const PhysicalMissionExecutionEngine& PhysicalMissionTelemetryLoop::mission() const noexcept { return mission_; }
const GroundStation& PhysicalMissionTelemetryLoop::ground_station() const noexcept { return ground_station_; }
const TelemetryIngestionPipeline& PhysicalMissionTelemetryLoop::telemetry() const noexcept { return telemetry_; }
const PhysicalMissionTelemetryLoopStats& PhysicalMissionTelemetryLoop::stats() const noexcept { return stats_; }

} // namespace trishula
