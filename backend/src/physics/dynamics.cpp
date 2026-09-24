#include "trishula/physics/dynamics.h"

#include <stdexcept>

namespace trishula {

Vector3 TranslationalDynamics::acceleration_from_force(const Vector3& total_force_newtons,
                                                        double mass_kg) const {
    if (mass_kg <= 0.0) {
        throw std::invalid_argument("Vehicle mass must be positive");
    }
    return total_force_newtons / mass_kg;
}

} // namespace trishula
