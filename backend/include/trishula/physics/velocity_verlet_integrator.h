#pragma once

#include "trishula/physics/state.h"

namespace trishula {

class VelocityVerletIntegrator {
public:
    [[nodiscard]] TrueState integrate(
        const TrueState& state,
        const Vector3& current_acceleration_m_per_s2,
        const Vector3& next_acceleration_m_per_s2,
        double dt_seconds) const;
};

} // namespace trishula
