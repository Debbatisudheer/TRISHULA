#include "trishula/maneuver/physical_lunar_powered_descent.h"
#include "trishula/mission/physical_mission_execution.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double kMuMoon = 4.9048695e12;
constexpr double kMoonRadius = 1.7374e6;
constexpr double kMoonOrbitRadius = 384400.0e3;
constexpr double kMoonPeriod = 27.321661 * 86400.0;
constexpr double kPi = 3.1415926535897932384626433832795;

trishula::Vector3 moon_position(double time_seconds, double phase_rad) {
    const double theta = phase_rad + 2.0 * kPi * time_seconds / kMoonPeriod;
    return {kMoonOrbitRadius * std::cos(theta), kMoonOrbitRadius * std::sin(theta), 0.0};
}

trishula::Vector3 moon_velocity(double time_seconds, double phase_rad) {
    const double theta = phase_rad + 2.0 * kPi * time_seconds / kMoonPeriod;
    const double omega = 2.0 * kPi / kMoonPeriod;
    return {-kMoonOrbitRadius * omega * std::sin(theta),
            kMoonOrbitRadius * omega * std::cos(theta), 0.0};
}
} // namespace

int main() {
    try {
        trishula::PhysicalMissionExecutionConfiguration mission_cfg{};
        mission_cfg.step_seconds = 1.0;
        mission_cfg.earth_orbit_hold_seconds = 10.0;
        mission_cfg.event_driven_physical_arc = true;

        trishula::PhysicalMissionExecutionEngine mission(mission_cfg);
        mission.run_until_phase(trishula::MissionPhase::LunarOrbit, 1000000U);
        (void)mission.execute_lunar_orbit_insertion();
        (void)mission.execute_lunar_orbit_operations();
        const auto& preparation = mission.execute_lunar_descent_preparation();
        if (!preparation.descent_ready) {
            throw std::runtime_error("V0.9.45 baseline is not descent-ready");
        }

        const double phase = mission.tli_plan().required_lunar_phase_angle_rad;
        trishula::PhysicalLunarPoweredDescentConfiguration descent_cfg{};
        descent_cfg.initiation_altitude_meters = 10000.0;
        descent_cfg.powered_descent_checkpoint_altitude_meters = 2000.0;
        descent_cfg.deorbit_target_perilune_altitude_meters = -20000.0;
        descent_cfg.control_step_seconds = 0.25;
        descent_cfg.maximum_descent_duration_seconds = 5000.0;
        descent_cfg.maximum_deorbit_delta_v_m_per_s = 100.0;

        trishula::PhysicalLunarPoweredDescentExecutor executor(
            descent_cfg,
            [phase](double t) { return moon_position(t, phase); },
            [phase](double t) { return moon_velocity(t, phase); },
            kMuMoon,
            kMoonRadius);

        const auto result = executor.execute(
            mission.simulation());

        if (!result.valid_initial_state || !result.deorbit_burn_executed ||
            !result.descent_entry_detected || !result.powered_descent_active) {
            throw std::runtime_error("physical powered descent initiation acceptance failed");
        }
        if (!(result.final_altitude_meters > descent_cfg.powered_descent_checkpoint_altitude_meters - 250.0 &&
              result.final_altitude_meters <= descent_cfg.initiation_altitude_meters)) {
            throw std::runtime_error("powered descent checkpoint altitude is invalid");
        }
        if (!(result.final_radial_velocity_m_per_s < 0.0)) {
            throw std::runtime_error("powered descent did not retain a physical descending radial state");
        }
        if (!(result.executed_deorbit_delta_v_m_per_s > 0.0 &&
              result.powered_descent_propellant_consumed_kg > 0.0)) {
            throw std::runtime_error("physical powered descent did not consume propulsion resources");
        }

        std::cout << std::fixed << std::setprecision(6)
                  << "TRISHULA V0.9.46 - Physical Powered Descent Initiation\n"
                  << "==============================================================\n"
                  << "  V0.9.45 descent-ready baseline : PASS\n"
                  << "  initial altitude                : " << result.initial_altitude_meters << " m\n"
                  << "  deorbit target perilune         : " << result.deorbit_target_perilune_altitude_meters << " m\n"
                  << "  planned deorbit delta-v         : " << result.planned_deorbit_delta_v_m_per_s << " m/s\n"
                  << "  executed deorbit delta-v        : " << result.executed_deorbit_delta_v_m_per_s << " m/s\n"
                  << "  deorbit burn duration           : " << result.deorbit_burn_duration_seconds << " s\n"
                  << "  deorbit propellant consumed     : " << result.deorbit_propellant_consumed_kg << " kg\n"
                  << "  descent entry altitude          : " << result.descent_entry_altitude_meters << " m\n"
                  << "  descent entry radial velocity   : " << result.descent_entry_radial_velocity_m_per_s << " m/s\n"
                  << "  descent entry tangential speed  : " << result.descent_entry_tangential_velocity_m_per_s << " m/s\n"
                  << "  powered descent duration        : " << result.powered_descent_duration_seconds << " s\n"
                  << "  powered descent propellant      : " << result.powered_descent_propellant_consumed_kg << " kg\n"
                  << "  final checkpoint altitude       : " << result.final_altitude_meters << " m\n"
                  << "  final radial velocity            : " << result.final_radial_velocity_m_per_s << " m/s\n"
                  << "  final tangential velocity        : " << result.final_tangential_velocity_m_per_s << " m/s\n"
                  << "  physical propagation steps      : " << result.physical_propagation_steps << "\n"
                  << "  deorbit burn executed            : YES\n"
                  << "  physical descent entry           : PASS\n"
                  << "  powered descent active           : PASS\n"
                  << "  physical powered descent init    : PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.46 campaign FAILED: " << ex.what() << '\n';
        return 1;
    }
}
