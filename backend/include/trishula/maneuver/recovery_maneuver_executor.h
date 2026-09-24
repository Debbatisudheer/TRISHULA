#pragma once

#include <cstddef>

#include "trishula/core/simulation_engine.h"
#include "trishula/maneuver/finite_burn_executor.h"
#include "trishula/maneuver/finite_burn_predictor.h"
#include "trishula/maneuver/maneuver_performance.h"
#include "trishula/maneuver/orbital_maneuver.h"
#include "trishula/estimation/state_estimator.h"

namespace trishula {

struct OrbitStageDiagnostic {
    NavigationState truth_orbit{};
    NavigationState estimated_orbit{};
    double position_estimation_error_m{0.0};
    double velocity_estimation_error_m_per_s{0.0};
};

struct RecoveryManeuverResult {
    OrbitalManeuverPlan plan{};
    NavigationState initial_orbit{};
    BurnExecutionSummary first_planned_burn{};
    NavigationState after_first_planned_burn{};
    BurnExecutionSummary first_correction_burn{};
    double first_correction_delta_v_m_per_s{0.0};
    int first_correction_direction_sign{0};
    NavigationState after_first_correction{};
    NavigationState before_second_burn{};
    BurnExecutionSummary second_planned_burn{};
    OrbitStageDiagnostic after_first_planned_diagnostic{};
    OrbitStageDiagnostic after_first_correction_diagnostic{};
    OrbitStageDiagnostic before_second_burn_diagnostic{};
    OrbitStageDiagnostic after_second_planned_diagnostic{};
    BurnExecutionSummary terminal_shaping_burn{};
    OrbitStageDiagnostic before_terminal_circularization_diagnostic{};
    BurnExecutionSummary second_correction_burn{};
    double second_correction_delta_v_m_per_s{0.0};
    int second_correction_direction_sign{0};
    NavigationState final_orbit{};
    NavigationState final_estimated_orbit{};
    double final_position_estimation_error_m{0.0};
    double final_velocity_estimation_error_m_per_s{0.0};
    double coast_duration_seconds{0.0};
    double terminal_shaping_delta_v_m_per_s{0.0};
    double terminal_circularization_delta_v_m_per_s{0.0};
    double terminal_coast_duration_seconds{0.0};
    std::size_t simulation_steps{0};
    bool apoapsis_event_detected{false};
    double first_burn_execution_scale_estimate{1.0};
    double adapted_second_burn_duration_seconds{0.0};
    double second_burn_execution_scale_estimate{1.0};
    double first_burn_performance_residual_m_per_s{0.0};
    double second_burn_performance_residual_m_per_s{0.0};
    double first_burn_duration_scale_estimate{1.0};
    double second_burn_duration_scale_estimate{1.0};
    double first_burn_reconstruction_residual_m_per_s{0.0};
    double second_burn_reconstruction_residual_m_per_s{0.0};
};

class RecoveryManeuverExecutor {
public:
    explicit RecoveryManeuverExecutor(
        double step_seconds = 0.1,
        EstimatorConfiguration estimator_configuration = {});

    [[nodiscard]] RecoveryManeuverResult execute(
        SimulationEngine& engine,
        const OrbitalManeuverPlan& plan) const;

private:
    [[nodiscard]] BurnExecutionSummary execute_burn(
        SimulationEngine& engine,
        StateEstimator& estimator,
        double duration_seconds,
        int direction_sign,
        std::size_t& simulation_steps) const;

    [[nodiscard]] BurnExecutionSummary execute_delta_v(
        SimulationEngine& engine,
        StateEstimator& estimator,
        double delta_v_m_per_s,
        std::size_t& simulation_steps) const;

    [[nodiscard]] NavigationState navigate_truth(const SimulationEngine& engine) const;
    [[nodiscard]] NavigationState navigate_estimated(
        const StateEstimator& estimator, const SimulationEngine& engine) const;

    static void update_estimator(
        StateEstimator& estimator,
        const SimulationEngine& engine,
        double reference_body_radius_meters,
        double step_seconds);

    [[nodiscard]] double duration_for_delta_v(
        const SimulationEngine& engine,
        double delta_v_m_per_s) const;

    double step_seconds_;
    EstimatorConfiguration estimator_configuration_{};
    double correction_deadband_m_per_s_{0.10};
    double maximum_correction_delta_v_m_per_s_{5.0};
    FiniteBurnPredictor burn_predictor_;
    ManeuverPerformanceEstimator performance_estimator_;
};

} // namespace trishula
