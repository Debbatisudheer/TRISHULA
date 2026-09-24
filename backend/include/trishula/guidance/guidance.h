#pragma once

#include "trishula/core/vector3.h"
#include "trishula/environment/celestial_body.h"
#include "trishula/navigation/navigation.h"

namespace trishula {

struct GuidanceTarget {
    double target_altitude_meters{400000.0};
    double target_tangential_speed_m_per_s{7672.594};
};

struct GuidanceCommand {
    Vector3 desired_position_meters{};
    Vector3 desired_velocity_m_per_s{};
    Vector3 commanded_acceleration_m_per_s2{};
    double radial_error_meters{0.0};
    double tangential_speed_error_m_per_s{0.0};
    double radial_velocity_m_per_s{0.0};
    double target_radius_meters{0.0};
};

class BaselineOrbitalGuidance {
public:
    explicit BaselineOrbitalGuidance(GuidanceTarget target,
                                     double radial_gain_per_s2 = 2.0e-5,
                                     double radial_velocity_damping_per_s = 4.0e-3,
                                     double tangential_gain_per_s = 1.0e-3,
                                     double acceleration_limit_m_per_s2 = 2.0);

    [[nodiscard]] GuidanceCommand compute(
        const NavigationState& navigation,
        const CelestialBody& primary_body) const;

    [[nodiscard]] const GuidanceTarget& target() const noexcept;

private:
    GuidanceTarget target_;
    double radial_gain_per_s2_;
    double radial_velocity_damping_per_s_;
    double tangential_gain_per_s_;
    double acceleration_limit_m_per_s2_;
};

} // namespace trishula
