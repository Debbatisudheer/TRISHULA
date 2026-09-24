#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "trishula/mission/physical_mission_phase_controller.h"

namespace trishula {

struct PhysicalMissionGroundUplinkConfiguration {
    PhysicalMissionExecutionConfiguration mission{};
    std::string api_url{"http://127.0.0.1:8082/v1/ingest"};
    std::string mission_id{"TRISHULA"};
    std::string source_node{"SPACECRAFT-01"};
    std::string origin_node{"SPACECRAFT-01"};
    std::string destination_node{"GS-TRISHULA-01"};
    std::uint16_t application_id{101U};
    std::size_t ticks{30U};
    std::uint32_t interval_ms{1000U};
};

struct PhysicalMissionGroundUplinkStats {
    std::uint64_t simulation_ticks{0U};
    std::uint64_t records_attempted{0U};
    std::uint64_t records_accepted{0U};
    std::uint64_t records_rejected{0U};
    std::uint64_t phase_transitions{0U};
};

class PhysicalMissionGroundUplink {
public:
    explicit PhysicalMissionGroundUplink(PhysicalMissionGroundUplinkConfiguration configuration = {});

    void reset();
    void step();
    void run();

    [[nodiscard]] const PhysicalMissionPhaseController& controller() const noexcept;
    [[nodiscard]] const PhysicalMissionGroundUplinkStats& stats() const noexcept;

private:
    struct Metric {
        const char* name;
        const char* subsystem;
        const char* unit;
        double value;
    };

    [[nodiscard]] std::string phase_name() const;
    [[nodiscard]] std::vector<Metric> metrics() const;
    [[nodiscard]] std::string make_record_json(const Metric& metric, std::uint64_t record_sequence);
    [[nodiscard]] bool post_json(const std::string& body, std::string& error) const;
    void publish_metric(const Metric& metric);

    PhysicalMissionGroundUplinkConfiguration configuration_{};
    PhysicalMissionPhaseController controller_{};
    PhysicalMissionGroundUplinkStats stats_{};
    std::uint64_t record_sequence_{0U};
    std::uint64_t mission_epoch_ns_{0U};
    std::uint64_t record_id_sequence_{0U};
};

} // namespace trishula
