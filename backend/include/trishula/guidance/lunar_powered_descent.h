#pragma once

#include "trishula/core/vector3.h"

namespace trishula {

struct LunarPoweredDescentCommand {
    Vector3 desired_acceleration_m_per_s2{};
    double throttle{0.0};
    bool landing_phase{false};
};

class LunarPoweredDescentController {
public:
    LunarPoweredDescentController(double max_thrust_newtons,
                                  double dry_mass_kg,
                                  double lunar_gravity_m_per_s2);

    [[nodiscard]] LunarPoweredDescentCommand compute(double altitude_m,
                                                      double horizontal_position_m,
                                                      double vertical_velocity_m_per_s,
                                                      double horizontal_velocity_m_per_s,
                                                      double mass_kg) const;

    [[nodiscard]] LunarPoweredDescentCommand computeToTarget(double altitude_m,
                                                              double horizontal_position_m,
                                                              double vertical_velocity_m_per_s,
                                                              double horizontal_velocity_m_per_s,
                                                              double mass_kg,
                                                              double target_x_m) const;

private:
    double max_thrust_newtons_;
    double dry_mass_kg_;
    double lunar_gravity_m_per_s2_;
};

} // namespace trishula
