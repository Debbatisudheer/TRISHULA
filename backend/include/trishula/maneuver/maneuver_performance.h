#pragma once

#include "trishula/core/vector3.h"
#include "trishula/physics/state.h"
#include "trishula/estimation/state_estimator.h"

namespace trishula {

struct ManeuverPerformanceEstimate {
    // State-transition reconstruction (diagnostic). This uses the estimated
    // pre/post state and an approximate gravity impulse.
    double observed_propulsive_delta_v_m_per_s{0.0};

    // Actuator-telemetry reconstruction. In the simulator this comes from
    // integrating the actual thrust acceleration delivered during the burn.
    double actuator_propulsive_delta_v_m_per_s{0.0};

    // Expected ideal propulsive delta-v for the measured burn duration at the
    // nominal thrust model.
    double nominal_propulsive_delta_v_m_per_s{0.0};

    // Thrust-performance estimate using actuator impulse. Because the nominal
    // reference uses the measured burn duration, duration changes do not look
    // like thrust changes.
    double effective_thrust_scale{1.0};

    // Ratio of measured burn duration to the nominal duration implied by the
    // requested delta-v at nominal propulsion performance.
    double duration_scale{1.0};

    // Difference between the actuator-derived and state-transition-derived
    // propulsive delta-v estimates.
    double reconstruction_residual_m_per_s{0.0};

    // Legacy diagnostic residual retained for compatibility.
    double residual_velocity_m_per_s{0.0};
    bool valid{false};
};

class ManeuverPerformanceEstimator {
public:
    explicit ManeuverPerformanceEstimator(
        double nominal_thrust_newtons,
        double specific_impulse_seconds,
        double gravitational_parameter_m3_per_s2);

    [[nodiscard]] ManeuverPerformanceEstimate estimate(
        const EstimatedState& pre_burn,
        const EstimatedState& post_burn,
        double duration_seconds,
        int direction_sign,
        double initial_mass_kg,
        double final_mass_kg) const;

    [[nodiscard]] ManeuverPerformanceEstimate estimate_from_actuator_measurement(
        const EstimatedState& pre_burn,
        const EstimatedState& post_burn,
        double duration_seconds,
        int direction_sign,
        double initial_mass_kg,
        double final_mass_kg,
        double actuator_propulsive_delta_v_m_per_s,
        double nominal_duration_seconds) const;

private:
    double nominal_delta_v_for_duration(
        double initial_mass_kg,
        double duration_seconds) const;

    double nominal_delta_v_for_mass_change(
        double initial_mass_kg,
        double final_mass_kg) const;

    double nominal_thrust_newtons_;
    double specific_impulse_seconds_;
    double gravitational_parameter_m3_per_s2_;
};

} // namespace trishula
