#pragma once

#include "trishula/core/vector3.h"

namespace trishula {

class CelestialBody {
public:
    CelestialBody(double gravitational_parameter,
                  double radius_meters,
                  Vector3 center_position_meters = {});

    [[nodiscard]] double gravitational_parameter() const noexcept;
    [[nodiscard]] double radius() const noexcept;
    [[nodiscard]] const Vector3& center_position() const noexcept;

private:
    double gravitational_parameter_m2_per_s2_;
    double radius_meters_;
    Vector3 center_position_meters_;
};

} // namespace trishula
