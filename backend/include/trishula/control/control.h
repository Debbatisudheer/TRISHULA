#pragma once

#include "trishula/core/vector3.h"
#include "trishula/guidance/guidance.h"
#include "trishula/estimation/state_estimator.h"
#include "trishula/physics/state.h"
#include "trishula/propulsion/propulsion.h"

namespace trishula {

struct ControlCommand {
    double throttle{0.0};
    Vector3 commanded_body_torque_nm{};
    Quaternion target_attitude_body_to_inertial{};
    Vector3 requested_thrust_inertial_newtons{};
    Vector3 commanded_thrust_direction_body{1.0, 0.0, 0.0};
    Vector3 commanded_rcs_force_body_newtons{};
    double thrust_vector_gimbal_angle_rad{0.0};
    Vector3 attitude_error_body{};
};

class BaselineAttitudeThrustController {
public:
    BaselineAttitudeThrustController(double attitude_gain = 8.0,
                                     double rate_damping = 4.0,
                                     double max_torque_nm = 20.0,
                                     double max_gimbal_angle_rad = 20.0 * 3.14159265358979323846 / 180.0,
                                     double rcs_force_limit_newtons = 25.0,
                                     double rcs_only_acceleration_threshold_m_per_s2 = 0.05);

    [[nodiscard]] ControlCommand compute(
        const EstimatedState& estimated_state,
        const GuidanceCommand& guidance,
        const Vector3& estimated_gravity_acceleration_m_per_s2,
        double vehicle_mass_kg,
        const MainEngine& engine,
        const RcsModule& rcs) const;

private:
    double attitude_gain_;
    double rate_damping_;
    double max_torque_nm_;
    double max_gimbal_angle_rad_;
    double rcs_force_limit_newtons_;
    double rcs_only_acceleration_threshold_m_per_s2_;
};

} // namespace trishula
