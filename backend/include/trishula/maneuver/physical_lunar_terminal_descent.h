#pragma once
#include <cstdint>
#include "trishula/core/simulation_engine.h"
#include <functional>
namespace trishula {
struct PhysicalLunarTerminalDescentConfiguration {
    double terminal_start_altitude_meters{2000.0};
    double terminal_target_altitude_meters{50.0};
    double control_step_seconds{0.1};
    double maximum_duration_seconds{600.0};
    double target_radial_velocity_m_per_s{-2.0};
    double maximum_throttle{1.0};
    double minimum_propellant_reserve_kg{100.0};
};
struct PhysicalLunarTerminalDescentResult {
    bool valid_initial_state{false}; bool terminal_descent_started{false}; bool terminal_descent_active{false}; bool propellant_budget_sufficient{false};
    double initial_altitude_meters{0.0}; double final_altitude_meters{0.0}; double initial_radial_velocity_m_per_s{0.0}; double final_radial_velocity_m_per_s{0.0};
    double propellant_consumed_kg{0.0}; double remaining_propellant_kg{0.0}; double duration_seconds{0.0}; std::uint64_t physical_propagation_steps{0};
};
class PhysicalLunarTerminalDescentExecutor {
public:
    using EphemerisFunction = std::function<Vector3(double)>;
    PhysicalLunarTerminalDescentExecutor(PhysicalLunarTerminalDescentConfiguration configuration,
                                         EphemerisFunction moon_position,
                                         EphemerisFunction moon_velocity,
                                         double moon_gravitational_parameter_m3_s2,
                                         double moon_radius_meters);
    [[nodiscard]] PhysicalLunarTerminalDescentResult execute(SimulationEngine& simulation) const;
private:
    [[nodiscard]] Vector3 relative_position(const SimulationEngine& simulation) const;
    [[nodiscard]] Vector3 relative_velocity(const SimulationEngine& simulation) const;
    [[nodiscard]] double altitude(const SimulationEngine& simulation) const;
    [[nodiscard]] double radial_velocity(const SimulationEngine& simulation) const;
    PhysicalLunarTerminalDescentConfiguration configuration_{};
    EphemerisFunction moon_position_{};
    EphemerisFunction moon_velocity_{};
    double moon_gravitational_parameter_m3_s2_{0.0};
    double moon_radius_meters_{0.0};
};
}
