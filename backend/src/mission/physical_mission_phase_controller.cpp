#include "trishula/mission/physical_mission_phase_controller.h"

#include <algorithm>
#include <stdexcept>

namespace trishula {

PhysicalMissionPhaseController::PhysicalMissionPhaseController(PhysicalMissionExecutionConfiguration configuration)
    : configuration_(configuration), mission_(configuration_) {
    reset();
}

void PhysicalMissionPhaseController::reset() {
    mission_.reset();
    transitions_.clear();
    snapshot_ = {};
    refresh(mission_.phase(), false);
}

double PhysicalMissionPhaseController::phase_duration(const MissionPhase phase) const noexcept {
    switch (phase) {
    case MissionPhase::EarthOrbit: return configuration_.earth_orbit_hold_seconds;
    case MissionPhase::TransLunarInjection: return configuration_.tli_burn_seconds;
    case MissionPhase::LunarCruise: return configuration_.lunar_cruise_seconds;
    case MissionPhase::LunarOrbit: return configuration_.lunar_orbit_hold_seconds;
    case MissionPhase::Descent: return configuration_.descent_seconds;
    case MissionPhase::Landing: return configuration_.landing_seconds;
    case MissionPhase::Launch:
    case MissionPhase::RoverDeployment:
    case MissionPhase::SurfaceOperations:
    case MissionPhase::Complete:
    case MissionPhase::Fault: return 0.0;
    }
    return 0.0;
}

void PhysicalMissionPhaseController::refresh(const MissionPhase previous, const bool changed) {
    const auto& state = mission_.snapshot();
    const double duration = phase_duration(state.phase);
    snapshot_.phase = state.phase;
    snapshot_.previous_phase = previous;
    snapshot_.mission_time_seconds = state.mission_time_seconds;
    snapshot_.phase_elapsed_seconds = state.phase_elapsed_seconds;
    snapshot_.phase_duration_seconds = duration;
    snapshot_.phase_progress = duration > 0.0
        ? std::clamp(state.phase_elapsed_seconds / duration, 0.0, 1.0)
        : (state.phase == MissionPhase::Complete ? 1.0 : 0.0);
    snapshot_.telemetry_sequence = state.telemetry_sequence;
    snapshot_.phase_transition_count = transitions_.size();
    snapshot_.phase_changed = changed;
    snapshot_.complete = mission_.phase() == MissionPhase::Complete;
}

void PhysicalMissionPhaseController::step() {
    const MissionPhase previous = mission_.phase();
    mission_.step();
    const MissionPhase current = mission_.phase();
    const bool changed = current != previous;
    if (changed) {
        transitions_.push_back({mission_.snapshot().telemetry_sequence,
                                mission_.snapshot().mission_time_seconds,
                                previous, current});
    }
    refresh(previous, changed);
}

void PhysicalMissionPhaseController::run(const std::uint64_t max_steps) {
    if (max_steps == 0U) throw std::invalid_argument("max_steps must be positive");
    std::uint64_t steps = 0U;
    while (!snapshot_.complete && steps < max_steps) {
        step();
        ++steps;
    }
    if (!snapshot_.complete) throw std::runtime_error("physical mission phase controller exceeded maximum steps");
}

const PhysicalMissionExecutionEngine& PhysicalMissionPhaseController::mission() const noexcept { return mission_; }
const PhysicalMissionPhaseControllerSnapshot& PhysicalMissionPhaseController::snapshot() const noexcept { return snapshot_; }
const std::vector<PhysicalMissionPhaseTransition>& PhysicalMissionPhaseController::transitions() const noexcept { return transitions_; }

} // namespace trishula
