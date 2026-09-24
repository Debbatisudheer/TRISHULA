#pragma once

#include <cstdint>
#include <vector>

#include "trishula/mission/physical_mission_execution.h"

namespace trishula {

struct PhysicalMissionPhaseControllerSnapshot {
    MissionPhase phase{MissionPhase::EarthOrbit};
    MissionPhase previous_phase{MissionPhase::EarthOrbit};
    double mission_time_seconds{0.0};
    double phase_elapsed_seconds{0.0};
    double phase_duration_seconds{0.0};
    double phase_progress{0.0};
    std::uint64_t telemetry_sequence{0U};
    std::uint64_t phase_transition_count{0U};
    bool phase_changed{false};
    bool complete{false};
};

struct PhysicalMissionPhaseTransition {
    std::uint64_t telemetry_sequence{0U};
    double mission_time_seconds{0.0};
    MissionPhase from{MissionPhase::EarthOrbit};
    MissionPhase to{MissionPhase::EarthOrbit};
};

class PhysicalMissionPhaseController {
public:
    explicit PhysicalMissionPhaseController(PhysicalMissionExecutionConfiguration configuration = {});

    void reset();
    void step();
    void run(std::uint64_t max_steps);

    [[nodiscard]] const PhysicalMissionExecutionEngine& mission() const noexcept;
    [[nodiscard]] const PhysicalMissionPhaseControllerSnapshot& snapshot() const noexcept;
    [[nodiscard]] const std::vector<PhysicalMissionPhaseTransition>& transitions() const noexcept;

private:
    [[nodiscard]] double phase_duration(MissionPhase phase) const noexcept;
    void refresh(MissionPhase previous, bool changed);

    PhysicalMissionExecutionConfiguration configuration_{};
    PhysicalMissionExecutionEngine mission_;
    PhysicalMissionPhaseControllerSnapshot snapshot_{};
    std::vector<PhysicalMissionPhaseTransition> transitions_{};
};

} // namespace trishula
