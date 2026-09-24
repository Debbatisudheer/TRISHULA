#pragma once

#include <cstddef>

#include "trishula/core/simulation_engine.h"
#include "trishula/maneuver/orbital_maneuver.h"
#include "trishula/navigation/navigation.h"

namespace trishula {

struct BurnExecutionSummary {
    double start_time_seconds{0.0};
    double end_time_seconds{0.0};
    double duration_seconds{0.0};
    double initial_mass_kg{0.0};
    double final_mass_kg{0.0};
    double propellant_consumed_kg{0.0};
    double achieved_propulsive_delta_v_m_per_s{0.0};
};

struct ManeuverExecutionResult {
    OrbitalManeuverPlan plan{};
    BurnExecutionSummary first_burn{};
    BurnExecutionSummary second_burn{};
    NavigationState initial_orbit{};
    NavigationState after_first_burn{};
    NavigationState before_second_burn{};
    NavigationState final_orbit{};
    double coast_duration_seconds{0.0};
    std::size_t simulation_steps{0};
    bool apoapsis_event_detected{false};
};

class HohmannFiniteBurnExecutor {
public:
    explicit HohmannFiniteBurnExecutor(double step_seconds = 0.1);

    [[nodiscard]] ManeuverExecutionResult execute(
        SimulationEngine& engine,
        const OrbitalManeuverPlan& plan) const;

private:
    [[nodiscard]] BurnExecutionSummary execute_burn(
        SimulationEngine& engine,
        double duration_seconds,
        int direction_sign,
        std::size_t& simulation_steps) const;

    [[nodiscard]] NavigationState navigate_truth(
        const SimulationEngine& engine) const;

    double step_seconds_;
};

} // namespace trishula
