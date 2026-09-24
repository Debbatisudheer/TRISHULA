#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "trishula/core/simulation_engine.h"
#include "trishula/mission/mission_execution.h"

namespace trishula {

struct PhysicalMissionExecutionConfiguration {
    double step_seconds{1.0};
    double earth_orbit_hold_seconds{60.0};
    double tli_burn_seconds{120.0};
    double lunar_cruise_seconds{432000.0};
    double lunar_orbit_hold_seconds{7200.0};
    double descent_seconds{1800.0};
    double landing_seconds{120.0};
    double surface_step_seconds{1.0};
    bool event_driven_physical_arc{false};
    double initial_mass_kg{50000.0};
};

struct LunarOrbitOperationsResult {
    bool orbit_bounded{false};
    bool repeated_apsides_detected{false};
    bool orbit_stable{false};
    bool station_keeping_effective{false};
    int revolutions_completed{0};
    double orbital_period_seconds{0.0};
    double baseline_semi_major_axis_meters{0.0};
    double baseline_eccentricity{0.0};
    double baseline_perilune_altitude_meters{0.0};
    double baseline_apolune_altitude_meters{0.0};
    double final_semi_major_axis_meters{0.0};
    double final_eccentricity{0.0};
    double final_perilune_altitude_meters{0.0};
    double final_apolune_altitude_meters{0.0};
    double disturbance_delta_v_m_per_s{0.0};
    double correction_delta_v_m_per_s{0.0};
    double disturbed_apolune_altitude_meters{0.0};
    double corrected_apolune_altitude_meters{0.0};
    double final_apolune_error_meters{0.0};
    double propellant_consumed_kg{0.0};
};

struct LunarDescentPreparationResult {
    bool descent_orbit_targeted{false};
    bool first_burn_executed{false};
    bool second_burn_executed{false};
    bool descent_orbit_bounded{false};
    bool descent_ready{false};
    double initial_perilune_altitude_meters{0.0};
    double initial_apolune_altitude_meters{0.0};
    double target_perilune_altitude_meters{0.0};
    double target_apolune_altitude_meters{0.0};
    double first_burn_planned_delta_v_m_per_s{0.0};
    double first_burn_executed_delta_v_m_per_s{0.0};
    double second_burn_planned_delta_v_m_per_s{0.0};
    double second_burn_executed_delta_v_m_per_s{0.0};
    double total_executed_delta_v_m_per_s{0.0};
    double final_perilune_altitude_meters{0.0};
    double final_apolune_altitude_meters{0.0};
    double final_semi_major_axis_meters{0.0};
    double final_eccentricity{0.0};
    double propellant_consumed_kg{0.0};
};

struct PhysicalMissionExecutionSnapshot {
    MissionPhase phase{MissionPhase::EarthOrbit};
    double mission_time_seconds{0.0};
    double phase_elapsed_seconds{0.0};
    double position_x_m{0.0};
    double position_y_m{0.0};
    double position_z_m{0.0};
    double velocity_x_m_per_s{0.0};
    double velocity_y_m_per_s{0.0};
    double velocity_z_m_per_s{0.0};
    double altitude_m{0.0};
    double speed_m_per_s{0.0};
    double distance_to_moon_m{0.0};
    double moon_x_m{0.0};
    double moon_y_m{0.0};
    std::uint64_t telemetry_sequence{0};
    double battery_soc{1.0};
    double thermal_temperature_c{20.0};
    double thermal_margin{1.0};
    double propellant_fraction{1.0};
    double thrust_fraction{0.0};
    bool engine_firing{false};
    bool rcs_firing{false};
    std::string power_status{"SUPPLIED"};
    std::string propulsion_status{"COASTING"};
    std::string thermal_status{"NOMINAL"};
    std::string communication_status{"ACQUIRING"};
    std::string navigation_status{"INITIALIZING"};
    std::string health_status{"NOMINAL"};
};

class PhysicalMissionExecutionEngine {
public:
    explicit PhysicalMissionExecutionEngine(PhysicalMissionExecutionConfiguration configuration = {});

    void reset();
    void step();
    void run_until_phase(MissionPhase target, std::uint64_t max_steps = 1000000);

    [[nodiscard]] const PhysicalMissionExecutionSnapshot& snapshot() const noexcept;
    [[nodiscard]] SimulationEngine& simulation() noexcept;
    [[nodiscard]] const SimulationEngine& simulation() const noexcept;
    [[nodiscard]] MissionPhase phase() const noexcept;
    [[nodiscard]] const TransLunarInjectionPlan& tli_plan() const noexcept;
    [[nodiscard]] const LunarOrbitInsertionResult& execute_lunar_orbit_insertion();
    [[nodiscard]] const LunarOrbitInsertionResult& lunar_orbit_insertion_result() const noexcept;
    [[nodiscard]] const LunarOrbitOperationsResult& execute_lunar_orbit_operations();
    [[nodiscard]] const LunarOrbitOperationsResult& lunar_orbit_operations_result() const noexcept;
    [[nodiscard]] const LunarDescentPreparationResult& execute_lunar_descent_preparation();
    [[nodiscard]] const LunarDescentPreparationResult& lunar_descent_preparation_result() const noexcept;

private:
    void propagate(const PropulsionCommand& command);
    void update_phase();
    void refresh_snapshot();
    void update_subsystem_model(double elapsed_seconds);
    [[nodiscard]] Vector3 moon_position(double time_seconds) const;
    [[nodiscard]] Vector3 moon_velocity(double time_seconds) const;

    PhysicalMissionExecutionConfiguration configuration_{};
    MissionPhase phase_{MissionPhase::EarthOrbit};
    double phase_elapsed_seconds_{0.0};
    std::uint64_t telemetry_sequence_{0};
    double tli_burn_remaining_seconds_{0.0};
    bool tli_burn_initialized_{false};
    TransLunarInjectionPlan tli_plan_{};
    LunarOrbitInsertionPlan loi_plan_{};
    std::unique_ptr<SimulationEngine> simulation_;
    PhysicalMissionExecutionSnapshot snapshot_{};
    LunarOrbitInsertionResult loi_result_{};
    bool loi_executed_{false};
    LunarOrbitOperationsResult lunar_orbit_operations_result_{};
    bool lunar_orbit_operations_executed_{false};
    LunarDescentPreparationResult lunar_descent_preparation_result_{};
    bool lunar_descent_preparation_executed_{false};
    double battery_soc_{1.0};
    double thermal_temperature_c_{20.0};
};

} // namespace trishula
