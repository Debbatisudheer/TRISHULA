#pragma once

#include "trishula/core/vector3.h"

#include <stdexcept>

namespace trishula {

struct DiagonalInertia {
    double ixx_kg_m2{1.0};
    double iyy_kg_m2{1.0};
    double izz_kg_m2{1.0};

    [[nodiscard]] Vector3 angular_momentum(const Vector3& angular_velocity_rad_s) const noexcept {
        return {
            ixx_kg_m2 * angular_velocity_rad_s.x,
            iyy_kg_m2 * angular_velocity_rad_s.y,
            izz_kg_m2 * angular_velocity_rad_s.z
        };
    }

    [[nodiscard]] Vector3 angular_acceleration(
        const Vector3& torque_nm,
        const Vector3& angular_velocity_rad_s) const {
        if (ixx_kg_m2 <= 0.0 || iyy_kg_m2 <= 0.0 || izz_kg_m2 <= 0.0) {
            throw std::invalid_argument("Principal moments of inertia must be positive");
        }

        const Vector3 angular_momentum_body = angular_momentum(angular_velocity_rad_s);
        const Vector3 gyroscopic_term = angular_velocity_rad_s.cross(angular_momentum_body);
        const Vector3 net_rotational_effect = torque_nm - gyroscopic_term;

        return {
            net_rotational_effect.x / ixx_kg_m2,
            net_rotational_effect.y / iyy_kg_m2,
            net_rotational_effect.z / izz_kg_m2
        };
    }
};

} // namespace trishula
