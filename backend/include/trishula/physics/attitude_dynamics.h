#pragma once

#include "trishula/math/quaternion.h"
#include "trishula/physics/inertia.h"

namespace trishula {

struct AttitudeState {
    Quaternion orientation_body_to_inertial{};
    Vector3 angular_velocity_rad_s{};
    Vector3 angular_acceleration_rad_s2{};
};

struct AttitudeDerivatives {
    Quaternion orientation_derivative{};
    Vector3 angular_acceleration_rad_s2{};
};

class AttitudeDynamics {
public:
    [[nodiscard]] AttitudeDerivatives derivatives(
        const Quaternion& orientation_body_to_inertial,
        const Vector3& angular_velocity_rad_s,
        const Vector3& torque_nm,
        const DiagonalInertia& inertia) const;
};

} // namespace trishula
