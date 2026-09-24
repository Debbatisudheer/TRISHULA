#pragma once

#include "trishula/core/vector3.h"
#include "trishula/math/quaternion.h"
#include "trishula/physics/state.h"

namespace trishula {

struct PropulsionCommand {
    double main_engine_throttle{0.0};
    Vector3 commanded_body_torque_nm{};
    Vector3 commanded_thrust_direction_body{1.0, 0.0, 0.0};
    Vector3 commanded_rcs_force_body_newtons{};
};

struct PropulsionOutput {
    Vector3 thrust_force_body_newtons{};
    Vector3 thrust_force_inertial_newtons{};
    Vector3 rcs_force_body_newtons{};
    Vector3 rcs_force_inertial_newtons{};
    Vector3 torque_body_nm{};
    double fuel_mass_flow_kg_per_s{0.0};
    double fuel_consumed_kg{0.0};
    double remaining_fuel_mass_kg{0.0};
    bool engine_firing{false};
    bool rcs_firing{false};
};

class MainEngine {
public:
    MainEngine(double maximum_thrust_newtons,
               double specific_impulse_seconds,
               double dry_mass_kg);

    [[nodiscard]] double maximum_thrust_newtons() const noexcept;
    [[nodiscard]] double specific_impulse_seconds() const noexcept;
    [[nodiscard]] double dry_mass_kg() const noexcept;

    [[nodiscard]] PropulsionOutput evaluate(
        const PropulsionCommand& command,
        const TrueState& state,
        double time_step_seconds) const;

private:
    double maximum_thrust_newtons_;
    double specific_impulse_seconds_;
    double dry_mass_kg_;
};

class RcsModule {
public:
    RcsModule(double maximum_force_newtons,
              double specific_impulse_seconds,
              double dry_mass_kg);

    [[nodiscard]] double maximum_force_newtons() const noexcept;
    [[nodiscard]] double specific_impulse_seconds() const noexcept;
    [[nodiscard]] PropulsionOutput evaluate(
        const PropulsionCommand& command,
        const TrueState& state,
        double time_step_seconds) const;

private:
    double maximum_force_newtons_;
    double specific_impulse_seconds_;
    double dry_mass_kg_;
};

} // namespace trishula
