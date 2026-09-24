#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "trishula/ground/space_link.h"
#include "trishula/ground/telemetry_ingestion.h"
#include "trishula/mission/physical_mission_execution.h"

namespace trishula {

struct PhysicalMissionTelemetryLoopConfiguration {
    PhysicalMissionExecutionConfiguration mission{};
    std::size_t telemetry_metrics_per_tick{11U};
    std::string source_node{"SPACECRAFT-01"};
    std::string origin_node{"SPACECRAFT-01"};
    std::string destination_node{"GS-TRISHULA-01"};
    std::uint16_t application_id{101U};
};

struct PhysicalMissionTelemetryLoopStats {
    std::uint64_t simulation_ticks{0U};
    std::uint64_t packets_encoded{0U};
    std::uint64_t packets_received{0U};
    std::uint64_t telemetry_records_accepted{0U};
    std::uint64_t rejected_packets{0U};
};

class PhysicalMissionTelemetryLoop {
public:
    explicit PhysicalMissionTelemetryLoop(PhysicalMissionTelemetryLoopConfiguration configuration = {});

    void reset();
    void step();
    void run(std::size_t ticks);

    [[nodiscard]] const PhysicalMissionExecutionEngine& mission() const noexcept;
    [[nodiscard]] const GroundStation& ground_station() const noexcept;
    [[nodiscard]] const TelemetryIngestionPipeline& telemetry() const noexcept;
    [[nodiscard]] const PhysicalMissionTelemetryLoopStats& stats() const noexcept;

private:
    struct TelemetryMetric {
        const char* name;
        const char* subsystem;
        const char* unit;
        double value;
    };

    std::vector<TelemetryMetric> build_metrics() const;
    void publish_metric(const TelemetryMetric& metric);

    PhysicalMissionTelemetryLoopConfiguration configuration_{};
    PhysicalMissionExecutionEngine mission_;
    GroundStation ground_station_{};
    TelemetryIngestionPipeline telemetry_{};
    PhysicalMissionTelemetryLoopStats stats_{};
    std::uint32_t packet_sequence_{0U};
    std::uint32_t frame_sequence_{0U};
};

} // namespace trishula
