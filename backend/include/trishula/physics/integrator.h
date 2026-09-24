#pragma once

#include "trishula/physics/state.h"

namespace trishula {

class EulerIntegrator {
public:
    [[nodiscard]] TrueState integrate(const TrueState& state,
                                      const Vector3& acceleration_m_per_s2,
                                      double dt_seconds) const;
};

} // namespace trishula
