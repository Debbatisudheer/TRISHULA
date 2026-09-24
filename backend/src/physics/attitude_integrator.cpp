#include "trishula/physics/attitude_integrator.h"

#include <stdexcept>

namespace trishula {

AttitudeState AttitudeEulerIntegrator::integrate(
    const AttitudeState& current,
    const Vector3& torque_nm,
    const DiagonalInertia& inertia,
    double time_step_seconds) const {
    if (time_step_seconds <= 0.0) {
        throw std::invalid_argument("Attitude integration time step must be positive");
    }

    const AttitudeDynamics dynamics;
    const AttitudeDerivatives derivative = dynamics.derivatives(
        current.orientation_body_to_inertial,
        current.angular_velocity_rad_s,
        torque_nm,
        inertia);

    const Vector3 next_angular_velocity =
        current.angular_velocity_rad_s +
        derivative.angular_acceleration_rad_s2 * time_step_seconds;

    const Quaternion next_orientation =
        (current.orientation_body_to_inertial +
         derivative.orientation_derivative * time_step_seconds).normalized();

    return {
        next_orientation,
        next_angular_velocity,
        derivative.angular_acceleration_rad_s2
    };
}

} // namespace trishula
