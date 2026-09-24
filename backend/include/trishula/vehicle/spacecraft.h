#pragma once

#include "trishula/physics/inertia.h"
#include "trishula/physics/state.h"

namespace trishula {

class Spacecraft {
public:
    Spacecraft(TrueState initial_state, DiagonalInertia inertia);

    [[nodiscard]] const TrueState& state() const noexcept;
    [[nodiscard]] const DiagonalInertia& inertia() const noexcept;
    void set_state(TrueState next_state);

private:
    TrueState state_;
    DiagonalInertia inertia_;
};

} // namespace trishula
