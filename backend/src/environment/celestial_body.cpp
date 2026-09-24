#include "trishula/environment/celestial_body.h"

#include <stdexcept>

namespace trishula {

CelestialBody::CelestialBody(double gravitational_parameter,
                             double radius_meters,
                             Vector3 center_position_meters)
    : gravitational_parameter_m2_per_s2_(gravitational_parameter),
      radius_meters_(radius_meters),
      center_position_meters_(center_position_meters) {
    if (gravitational_parameter <= 0.0) {
        throw std::invalid_argument("Gravitational parameter must be positive");
    }
    if (radius_meters <= 0.0) {
        throw std::invalid_argument("Celestial body radius must be positive");
    }
}

double CelestialBody::gravitational_parameter() const noexcept {
    return gravitational_parameter_m2_per_s2_;
}

double CelestialBody::radius() const noexcept { return radius_meters_; }

const Vector3& CelestialBody::center_position() const noexcept {
    return center_position_meters_;
}

} // namespace trishula
