#include "trishula/physics/integrator.h"

#include <stdexcept>

namespace trishula {

TrueState EulerIntegrator::integrate(const TrueState& state,
                                     const Vector3& acceleration_m_per_s2,
                                     double dt_seconds) const {
    if (dt_seconds <= 0.0) {
        throw std::invalid_argument("Integrator time step must be positive");
    }

    TrueState next = state;
    next.position_meters = state.position_meters + state.velocity_m_per_s * dt_seconds;
    next.velocity_m_per_s = state.velocity_m_per_s + acceleration_m_per_s2 * dt_seconds;
    next.acceleration_m_per_s2 = acceleration_m_per_s2;
    next.time_seconds = state.time_seconds + dt_seconds;
    return next;
}

} // namespace trishula
