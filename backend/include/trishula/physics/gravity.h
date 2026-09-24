#pragma once

#include "trishula/core/vector3.h"
#include "trishula/environment/celestial_body.h"

namespace trishula {

class GravityModel {
public:
    [[nodiscard]] Vector3 acceleration(const CelestialBody& body,
                                       const Vector3& object_position_meters) const;
};

} // namespace trishula
