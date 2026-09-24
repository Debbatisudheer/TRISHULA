#pragma once

#include "trishula/physics/attitude_dynamics.h"

namespace trishula {

class AttitudeEulerIntegrator {
public:
    [[nodiscard]] AttitudeState integrate(
        const AttitudeState& current,
        const Vector3& torque_nm,
        const DiagonalInertia& inertia,
        double time_step_seconds) const;
};

} // namespace trishula
