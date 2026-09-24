#pragma once

#include "trishula/core/vector3.h"
#include "trishula/math/quaternion.h"

namespace trishula {

struct TrueState {
    double time_seconds{0.0};
    Vector3 position_meters{};
    Vector3 velocity_m_per_s{};
    Vector3 acceleration_m_per_s2{};

    Quaternion attitude_body_to_inertial{};
    Vector3 angular_velocity_rad_s{};
    Vector3 angular_acceleration_rad_s2{};

    double mass_kg{0.0};
};

} // namespace trishula
