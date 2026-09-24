#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "trishula/rover/surface_rover.h"
#include "trishula/rover/multi_objective_mission_planner.h"

namespace trishula {

enum class ScienceTaskStatus { Pending, EnRoute, Stabilizing, Acquiring, Validated, Stored, Failed };

struct RoverScienceObservation {
    std::size_t target_id{0};
    double quality{0.0};
    double energy_used_wh{0.0};
    bool instrument_healthy{true};
    bool acquired{false};
    bool validated{false};
    bool stored{false};
};

struct RoverScienceExecutionResult {
    bool mission_completed{false};
    std::vector<RoverScienceObservation> observations{};
    std::size_t executed_targets{0};
    double science_data_score{0.0};
    double science_energy_used_wh{0.0};
    bool rover_stable{false};
    bool data_store_healthy{true};
};

class RoverScienceOperations {
public:
    explicit RoverScienceOperations(double minimum_quality = 0.75,
                                    double energy_per_target_wh = 12.0);

    [[nodiscard]] RoverScienceExecutionResult execute_target(
        RoverState& rover,
        const RoverScienceTarget& target,
        bool instrument_healthy = true,
        bool data_store_healthy = true) const;

private:
    double minimum_quality_{0.75};
    double energy_per_target_wh_{12.0};
};

} // namespace trishula
