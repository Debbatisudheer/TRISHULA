#pragma once

#include <cstdint>

#include "trishula/mission/physical_mission_execution.h"

namespace trishula {

enum class PhysicalMissionRuntimeState {
    Ready,
    Starting,
    Running,
    Paused,
    Resuming,
    Completed,
    Aborted,
    Fault,
};

struct PhysicalMissionRuntimeSnapshot {
    PhysicalMissionRuntimeState state{PhysicalMissionRuntimeState::Ready};
    PhysicalMissionExecutionSnapshot physical{};
    std::uint64_t control_sequence{0U};
};

class PhysicalMissionRuntimeController {
public:
    explicit PhysicalMissionRuntimeController(PhysicalMissionExecutionConfiguration configuration = {});

    void reset();
    void start();
    void pause();
    void resume();
    void abort();
    bool step();

    [[nodiscard]] const PhysicalMissionRuntimeSnapshot& snapshot() const noexcept;
    [[nodiscard]] const PhysicalMissionExecutionEngine& mission() const noexcept;
    [[nodiscard]] PhysicalMissionExecutionEngine& mission() noexcept;

private:
    void refresh();

    PhysicalMissionExecutionEngine mission_;
    PhysicalMissionRuntimeSnapshot snapshot_{};
};

} // namespace trishula
