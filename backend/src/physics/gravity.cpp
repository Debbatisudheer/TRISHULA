#include "trishula/physics/gravity.h"

#include <stdexcept>

namespace trishula {

Vector3 GravityModel::acceleration(const CelestialBody& body,
                                   const Vector3& object_position_meters) const {
    const Vector3 relative_position = object_position_meters - body.center_position();
    const double distance_squared = relative_position.magnitude_squared();

    if (distance_squared == 0.0) {
        throw std::invalid_argument("Gravity is undefined at the celestial-body center");
    }

    const double distance = relative_position.magnitude();
    const double factor = -body.gravitational_parameter() / (distance_squared * distance);
    return relative_position * factor;
}

} // namespace trishula
