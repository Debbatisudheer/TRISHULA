#pragma once

#include "trishula/core/vector3.h"

namespace trishula {

class TranslationalDynamics {
public:
    [[nodiscard]] Vector3 acceleration_from_force(const Vector3& total_force_newtons,
                                                   double mass_kg) const;
};

} // namespace trishula
