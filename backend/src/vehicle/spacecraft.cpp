#include "trishula/vehicle/spacecraft.h"

namespace trishula {

Spacecraft::Spacecraft(TrueState initial_state, DiagonalInertia inertia)
    : state_(initial_state), inertia_(inertia) {}

const TrueState& Spacecraft::state() const noexcept { return state_; }

const DiagonalInertia& Spacecraft::inertia() const noexcept { return inertia_; }

void Spacecraft::set_state(TrueState next_state) { state_ = next_state; }

} // namespace trishula
