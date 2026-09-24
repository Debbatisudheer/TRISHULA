#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "trishula/ground/closed_loop_feedback.h"

namespace trishula {

enum class ClosedLoopMissionCycleStatus {
    Applied,
    EmptyCycle,
    SequenceViolation,
    CommandFailed
};

struct ClosedLoopMissionCycleResult {
    ClosedLoopMissionCycleStatus status{ClosedLoopMissionCycleStatus::EmptyCycle};
    std::string message{};
    std::size_t commands_processed{0U};
    std::size_t events_ingested{0U};
    std::size_t telemetry_records_ingested{0U};
    std::uint64_t physical_steps_before{0U};
    std::uint64_t physical_steps_after{0U};
};

// V0.9.55: executes an ordered mission-control command cycle through the
// existing physical closed-loop feedback engine. It never advances physical
// state for a command that fails logical execution and stops the cycle at the
// first failed command.
class GroundClosedLoopMissionCycle {
public:
    explicit GroundClosedLoopMissionCycle(ClosedLoopFeedbackConfiguration configuration = {});

    [[nodiscard]] const GroundClosedLoopFeedbackEngine& feedback_engine() const noexcept;

    ClosedLoopMissionCycleResult process(const std::vector<GroundCommandRecord>& commands);
    void reset() noexcept;

private:
    static bool strictly_increasing_sequences(const std::vector<GroundCommandRecord>& commands) noexcept;

    GroundClosedLoopFeedbackEngine feedback_engine_{};
};

} // namespace trishula
