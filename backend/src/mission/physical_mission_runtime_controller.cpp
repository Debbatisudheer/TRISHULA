#include "trishula/mission/physical_mission_runtime_controller.h"

#include <stdexcept>

namespace trishula {

PhysicalMissionRuntimeController::PhysicalMissionRuntimeController(
    PhysicalMissionExecutionConfiguration configuration)
    : mission_(configuration) {
    reset();
}

void PhysicalMissionRuntimeController::reset() {
    mission_.reset();
    snapshot_ = {};
    snapshot_.state = PhysicalMissionRuntimeState::Ready;
    refresh();
}

void PhysicalMissionRuntimeController::start() {
    if (snapshot_.state != PhysicalMissionRuntimeState::Ready) {
        throw std::runtime_error("mission start requires Ready state");
    }
    snapshot_.state = PhysicalMissionRuntimeState::Starting;
    ++snapshot_.control_sequence;
    snapshot_.state = PhysicalMissionRuntimeState::Running;
    refresh();
}

void PhysicalMissionRuntimeController::pause() {
    if (snapshot_.state != PhysicalMissionRuntimeState::Running) {
        throw std::runtime_error("mission pause requires Running state");
    }
    snapshot_.state = PhysicalMissionRuntimeState::Paused;
    ++snapshot_.control_sequence;
    refresh();
}

void PhysicalMissionRuntimeController::resume() {
    if (snapshot_.state != PhysicalMissionRuntimeState::Paused) {
        throw std::runtime_error("mission resume requires Paused state");
    }
    snapshot_.state = PhysicalMissionRuntimeState::Resuming;
    ++snapshot_.control_sequence;
    snapshot_.state = PhysicalMissionRuntimeState::Running;
    refresh();
}

void PhysicalMissionRuntimeController::abort() {
    if (snapshot_.state != PhysicalMissionRuntimeState::Running &&
        snapshot_.state != PhysicalMissionRuntimeState::Paused) {
        throw std::runtime_error("mission abort requires Running or Paused state");
    }
    snapshot_.state = PhysicalMissionRuntimeState::Aborted;
    ++snapshot_.control_sequence;
    refresh();
}

bool PhysicalMissionRuntimeController::step() {
    if (snapshot_.state == PhysicalMissionRuntimeState::Completed ||
        snapshot_.state == PhysicalMissionRuntimeState::Aborted ||
        snapshot_.state == PhysicalMissionRuntimeState::Fault) {
        return false;
    }
    if (snapshot_.state != PhysicalMissionRuntimeState::Running) {
        refresh();
        return false;
    }

    try {
        mission_.step();
        refresh();
        if (mission_.phase() == MissionPhase::Complete) {
            snapshot_.state = PhysicalMissionRuntimeState::Completed;
            ++snapshot_.control_sequence;
            refresh();
        }
        return true;
    } catch (...) {
        snapshot_.state = PhysicalMissionRuntimeState::Fault;
        ++snapshot_.control_sequence;
        refresh();
        throw;
    }
}

void PhysicalMissionRuntimeController::refresh() {
    snapshot_.physical = mission_.snapshot();
}

const PhysicalMissionRuntimeSnapshot& PhysicalMissionRuntimeController::snapshot() const noexcept {
    return snapshot_;
}

const PhysicalMissionExecutionEngine& PhysicalMissionRuntimeController::mission() const noexcept {
    return mission_;
}

PhysicalMissionExecutionEngine& PhysicalMissionRuntimeController::mission() noexcept {
    return mission_;
}

} // namespace trishula
