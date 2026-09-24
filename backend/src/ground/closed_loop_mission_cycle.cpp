#include "trishula/ground/closed_loop_mission_cycle.h"

namespace trishula {

GroundClosedLoopMissionCycle::GroundClosedLoopMissionCycle(ClosedLoopFeedbackConfiguration configuration)
    : feedback_engine_(configuration) {}

const GroundClosedLoopFeedbackEngine& GroundClosedLoopMissionCycle::feedback_engine() const noexcept {
    return feedback_engine_;
}

bool GroundClosedLoopMissionCycle::strictly_increasing_sequences(
    const std::vector<GroundCommandRecord>& commands) noexcept {
    if (commands.empty()) return true;
    for (std::size_t index = 1U; index < commands.size(); ++index) {
        if (commands[index].sequence_number <= commands[index - 1U].sequence_number) return false;
    }
    return true;
}

ClosedLoopMissionCycleResult GroundClosedLoopMissionCycle::process(
    const std::vector<GroundCommandRecord>& commands) {
    ClosedLoopMissionCycleResult result{};
    if (commands.empty()) {
        result.status = ClosedLoopMissionCycleStatus::EmptyCycle;
        result.message = "closed-loop mission cycle contains no commands";
        return result;
    }

    if (!strictly_increasing_sequences(commands)) {
        result.status = ClosedLoopMissionCycleStatus::SequenceViolation;
        result.message = "closed-loop mission cycle command sequences must be strictly increasing";
        return result;
    }

    const auto& adapter = feedback_engine_.vehicle_adapter();
    result.physical_steps_before = adapter.snapshot().physical_steps;

    for (const auto& command : commands) {
        const auto cycle_result = feedback_engine_.process(command);
        result.events_ingested += cycle_result.events_ingested;
        result.telemetry_records_ingested += cycle_result.telemetry_records_ingested;

        if (cycle_result.status != ClosedLoopStatus::Applied) {
            result.status = ClosedLoopMissionCycleStatus::CommandFailed;
            result.message = "closed-loop mission cycle stopped at command_id=" + command.command_id
                           + ": " + cycle_result.message;
            result.physical_steps_after = adapter.snapshot().physical_steps;
            return result;
        }

        ++result.commands_processed;
    }

    result.status = ClosedLoopMissionCycleStatus::Applied;
    result.physical_steps_after = adapter.snapshot().physical_steps;
    result.message = "closed-loop mission cycle executed with physical feedback for every command";
    return result;
}

void GroundClosedLoopMissionCycle::reset() noexcept {
    feedback_engine_.reset();
}

} // namespace trishula
