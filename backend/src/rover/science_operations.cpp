#include "trishula/rover/science_operations.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {

RoverScienceOperations::RoverScienceOperations(double minimum_quality,
                                                 double energy_per_target_wh)
    : minimum_quality_(minimum_quality), energy_per_target_wh_(energy_per_target_wh) {
    if (minimum_quality_ < 0.0 || minimum_quality_ > 1.0 || energy_per_target_wh_ <= 0.0) {
        throw std::invalid_argument("Invalid science operations configuration");
    }
}

RoverScienceExecutionResult RoverScienceOperations::execute_target(
    RoverState& rover,
    const RoverScienceTarget& target,
    bool instrument_healthy,
    bool data_store_healthy) const {
    RoverScienceExecutionResult result{};
    result.rover_stable = rover.deployed && rover.localization_valid && rover.drive_system_healthy;
    result.data_store_healthy = data_store_healthy;

    RoverScienceObservation observation{};
    observation.target_id = target.id;
    observation.energy_used_wh = energy_per_target_wh_;
    observation.instrument_healthy = instrument_healthy;

    if (!result.rover_stable || !instrument_healthy || !data_store_healthy) {
        observation.quality = 0.0;
        result.observations.push_back(observation);
        return result;
    }

    // Deterministic engineering observation quality based on target science value/priority.
    const double quality = std::clamp(
        0.55 + 0.012 * target.science_priority + 0.008 * target.science_value,
        0.0, 0.99);
    observation.quality = quality;

    if (rover.battery_soc <= 0.0 || energy_per_target_wh_ > rover.battery_soc * 1000.0) {
        result.observations.push_back(observation);
        return result;
    }

    observation.acquired = true;
    observation.validated = quality >= minimum_quality_;
    observation.stored = observation.validated;
    result.observations.push_back(observation);

    if (observation.stored) {
        rover.battery_soc = std::max(0.0, rover.battery_soc - energy_per_target_wh_ / 1000.0);
        result.executed_targets = 1;
        result.science_data_score = target.science_value * quality;
        result.science_energy_used_wh = energy_per_target_wh_;
        result.mission_completed = true;
    }

    return result;
}

} // namespace trishula
