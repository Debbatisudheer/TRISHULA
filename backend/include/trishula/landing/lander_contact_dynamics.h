#pragma once

#include "trishula/core/vector3.h"
#include "trishula/math/quaternion.h"

#include <array>
#include <functional>

namespace trishula {

struct LanderContactState {
    Vector3 position_m{};
    Vector3 velocity_m_s{};
    Quaternion attitude_body_to_inertial{};
    Vector3 angular_velocity_rad_s{};
    double mass_kg{1.0};
    double time_s{0.0};
};

struct LanderContactGeometry {
    std::array<Vector3, 4> leg_attach_points_body_m{
        Vector3{ 1.0,  1.0, -1.2},
        Vector3{ 1.0, -1.0, -1.2},
        Vector3{-1.0,  1.0, -1.2},
        Vector3{-1.0, -1.0, -1.2}
    };
};

struct LanderContactParameters {
    double lunar_gravity_m_s2{1.62};
    double spring_n_m{120000.0};
    double damping_n_s_m{50000.0};
    double friction_coefficient{0.6};
    double max_leg_force_n{200000.0};
    double ixx_kg_m2{26000.0};
    double iyy_kg_m2{26000.0};
    double izz_kg_m2{40000.0};
};

struct LanderContactMetrics {
    std::array<double, 4> normal_force_n{};
    std::array<bool, 4> in_contact{};
    double maximum_contact_force_n{0.0};
    double maximum_shock_acceleration_m_s2{0.0};
    double tilt_rad{0.0};
    double angular_rate_rad_s{0.0};
    double touchdown_time_s{-1.0};
    bool touchdown_detected{false};
    bool stable_contact{false};
    bool tip_over_detected{false};
    bool landed_state{false};
};

class LanderContactDynamics {
public:
    using TerrainHeightFunction = std::function<double(double, double)>;

    LanderContactDynamics(
        LanderContactParameters parameters,
        LanderContactGeometry geometry = {},
        TerrainHeightFunction terrain_height = {});

    [[nodiscard]] LanderContactMetrics step(LanderContactState& state, double dt_s) const;
    [[nodiscard]] static double tilt_from_vertical_rad(const Quaternion& attitude_body_to_inertial);
    [[nodiscard]] static Vector3 world_from_body(const Quaternion& attitude_body_to_inertial, const Vector3& body_vector);

private:
    LanderContactParameters parameters_{};
    LanderContactGeometry geometry_{};
    TerrainHeightFunction terrain_height_{};
};

} // namespace trishula
