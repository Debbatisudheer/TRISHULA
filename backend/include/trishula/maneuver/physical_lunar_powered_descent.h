#pragma once

#include <functional>
#include <cstdint>

#include "trishula/core/simulation_engine.h"

namespace trishula {

struct PhysicalLunarPoweredDescentConfiguration {
    double initiation_altitude_meters{10000.0};
    double powered_descent_checkpoint_altitude_meters{2000.0};
    double deorbit_target_perilune_altitude_meters{-20000.0};
    double control_step_seconds{0.25};
    double maximum_descent_duration_seconds{5000.0};
    double maximum_deorbit_delta_v_m_per_s{100.0};
};

struct PhysicalLunarPoweredDescentResult {
    bool valid_initial_state{false};
    bool deorbit_burn_executed{false};
    bool descent_entry_detected{false};
    bool powered_descent_active{false};
    double initial_altitude_meters{0.0};
    double deorbit_target_perilune_altitude_meters{0.0};
    double planned_deorbit_delta_v_m_per_s{0.0};
    double executed_deorbit_delta_v_m_per_s{0.0};
    double deorbit_burn_duration_seconds{0.0};
    double deorbit_propellant_consumed_kg{0.0};
    double descent_entry_altitude_meters{0.0};
    double descent_entry_radial_velocity_m_per_s{0.0};
    double descent_entry_tangential_velocity_m_per_s{0.0};
    double final_altitude_meters{0.0};
    double final_radial_velocity_m_per_s{0.0};
    double final_tangential_velocity_m_per_s{0.0};
    double powered_descent_duration_seconds{0.0};
    double powered_descent_propellant_consumed_kg{0.0};
    std::uint64_t physical_propagation_steps{0};
};

class PhysicalLunarPoweredDescentExecutor {
public:
    using EphemerisFunction = std::function<Vector3(double)>;

    PhysicalLunarPoweredDescentExecutor(
        PhysicalLunarPoweredDescentConfiguration configuration,
        EphemerisFunction moon_position,
        EphemerisFunction moon_velocity,
        double moon_gravitational_parameter_m3_s2,
        double moon_radius_meters);

    [[nodiscard]] PhysicalLunarPoweredDescentResult execute(SimulationEngine& simulation) const;

private:
    [[nodiscard]] Vector3 relative_position(
        const SimulationEngine& simulation) const;
    [[nodiscard]] Vector3 relative_velocity(
        const SimulationEngine& simulation) const;
    [[nodiscard]] double altitude(
        const SimulationEngine& simulation) const;
    [[nodiscard]] double radial_velocity(
        const SimulationEngine& simulation) const;
    [[nodiscard]] double tangential_speed(
        const SimulationEngine& simulation) const;

    [[nodiscard]] double delta_v_for_target_perilune(
        const SimulationEngine& simulation,
        double target_perilune_radius_meters) const;

    [[nodiscard]] double burn_duration_for_delta_v(
        const SimulationEngine& simulation,
        double delta_v_m_per_s) const;

    [[nodiscard]] double execute_retrograde_burn(
        SimulationEngine& simulation,
        double delta_v_m_per_s) const;

    PhysicalLunarPoweredDescentConfiguration configuration_{};
    EphemerisFunction moon_position_{};
    EphemerisFunction moon_velocity_{};
    double moon_gravitational_parameter_m3_s2_{0.0};
    double moon_radius_meters_{0.0};
};

} // namespace trishula
