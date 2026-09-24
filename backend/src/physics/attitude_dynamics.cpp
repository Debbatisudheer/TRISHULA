#include "trishula/physics/attitude_dynamics.h"

namespace trishula {

AttitudeDerivatives AttitudeDynamics::derivatives(
    const Quaternion& orientation_body_to_inertial,
    const Vector3& angular_velocity_rad_s,
    const Vector3& torque_nm,
    const DiagonalInertia& inertia) const {
    const Quaternion omega_quaternion{
        0.0,
        angular_velocity_rad_s.x,
        angular_velocity_rad_s.y,
        angular_velocity_rad_s.z};

    // For a body-to-inertial quaternion, q_dot = 1/2 q ⊗ [0, ω_body].
    const Quaternion orientation_derivative =
        (orientation_body_to_inertial * omega_quaternion) * 0.5;

    return {
        orientation_derivative,
        inertia.angular_acceleration(torque_nm, angular_velocity_rad_s)
    };
}

} // namespace trishula
