#pragma once

#include <cstddef>

#include "trishula/core/simulation_engine.h"
#include "trishula/estimation/state_estimator.h"
#include "trishula/navigation/navigation.h"

namespace trishula {

struct PredictedBurnSolution {
    double delta_v_m_per_s{0.0};
    int direction_sign{0};
    double duration_seconds{0.0};
    double predicted_semi_major_axis_meters{0.0};
    double predicted_eccentricity{0.0};
    double prediction_error_meters{0.0};
    bool valid{false};
};

class FiniteBurnPredictor {
public:
    explicit FiniteBurnPredictor(double time_step_seconds = 0.1);

    [[nodiscard]] PredictedBurnSolution solve(
        const SimulationEngine& engine,
        const StateEstimator& estimator,
        double target_semi_major_axis_meters,
        double maximum_delta_v_m_per_s) const;

    [[nodiscard]] PredictedBurnSolution solve_for_periapsis(
        const SimulationEngine& engine,
        const StateEstimator& estimator,
        double target_periapsis_radius_meters,
        double maximum_delta_v_m_per_s) const;

    // Predicts a finite burn, then explicitly propagates the post-burn coast
    // to the next periapsis using the active perturbation model. This closes
    // the gap between instantaneous osculating targeting and the actual
    // terminal event used by the maneuver executor.
    [[nodiscard]] PredictedBurnSolution solve_for_periapsis_after_coast(
        const SimulationEngine& engine,
        const StateEstimator& estimator,
        double target_periapsis_radius_meters,
        double maximum_delta_v_m_per_s,
        double maximum_coast_seconds = 0.0) const;

private:
    [[nodiscard]] NavigationState predict(
        const SimulationEngine& engine,
        const StateEstimator& estimator,
        double delta_v_m_per_s,
        int direction_sign) const;

    [[nodiscard]] double duration_for_delta_v(
        const SimulationEngine& engine,
        double delta_v_m_per_s) const;

    [[nodiscard]] int direction_for_target(
        const SimulationEngine& engine,
        const StateEstimator& estimator,
        double target_semi_major_axis_meters) const;

    double step_seconds_;
};

} // namespace trishula
