#include "trishula/core/simulation_clock.h"

#include <stdexcept>

namespace trishula {

SimulationClock::SimulationClock(double time_step_seconds)
    : current_time_seconds_(0.0), time_step_seconds_(time_step_seconds) {
    if (time_step_seconds <= 0.0) {
        throw std::invalid_argument("Simulation time step must be positive");
    }
}

double SimulationClock::time() const noexcept { return current_time_seconds_; }

double SimulationClock::time_step() const noexcept { return time_step_seconds_; }

void SimulationClock::advance() { current_time_seconds_ += time_step_seconds_; }

void SimulationClock::advance(double delta_seconds) {
    if (delta_seconds <= 0.0) {
        throw std::invalid_argument("Simulation clock advance must be positive");
    }
    current_time_seconds_ += delta_seconds;
}

void SimulationClock::reset(double start_time_seconds) {
    current_time_seconds_ = start_time_seconds;
}

} // namespace trishula
