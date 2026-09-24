#include "trishula/landing/physical_lunar_landing_integration.h"
#include "trishula/maneuver/physical_lunar_powered_descent.h"
#include "trishula/maneuver/physical_lunar_terminal_descent.h"
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
constexpr double kPi = 3.141592653589793238462643383279502884;

trishula::Vector3 moon_position(double t, double phase) {
    const double theta = phase + 2.0 * kPi * t / kMoonPeriod;
    return {kMoonOrbitRadius * std::cos(theta),
            kMoonOrbitRadius * std::sin(theta),
            0.0};
}

trishula::Vector3 moon_velocity(double t, double phase) {
    const double theta = phase + 2.0 * kPi * t / kMoonPeriod;
    const double omega = 2.0 * kPi / kMoonPeriod;
    return {-kMoonOrbitRadius * omega * std::sin(theta),
            kMoonOrbitRadius * omega * std::cos(theta),
            0.0};
}
}

int main() {
    try {
        trishula::PhysicalMissionExecutionConfiguration mission_configuration{};
        mission_configuration.step_seconds = 1.0;
        mission_configuration.earth_orbit_hold_seconds = 10.0;
        mission_configuration.event_driven_physical_arc = true;
        mission_configuration.initial_mass_kg = 90000.0;

        trishula::PhysicalMissionExecutionEngine mission(mission_configuration);
        mission.run_until_phase(trishula::MissionPhase::LunarOrbit, 1000000U);

        (void)mission.execute_lunar_orbit_insertion();
        (void)mission.execute_lunar_orbit_operations();

        const auto descent = mission.execute_lunar_descent_preparation();
        if (!descent.descent_ready) {
            throw std::runtime_error("V0.9.45 baseline is not descent-ready");
        }

        const double phase = mission.tli_plan().required_lunar_phase_angle_rad;

        trishula::PhysicalLunarPoweredDescentConfiguration powered_configuration{};
        powered_configuration.initiation_altitude_meters = 10000.0;
        powered_configuration.powered_descent_checkpoint_altitude_meters = 2000.0;
        powered_configuration.deorbit_target_perilune_altitude_meters = -20000.0;
        powered_configuration.control_step_seconds = 0.25;
        powered_configuration.maximum_descent_duration_seconds = 5000.0;
        powered_configuration.maximum_deorbit_delta_v_m_per_s = 100.0;

        trishula::PhysicalLunarPoweredDescentExecutor powered_executor(
            powered_configuration,
            [phase](double t) { return moon_position(t, phase); },
            [phase](double t) { return moon_velocity(t, phase); },
            kMuMoon,
            kMoonRadius);

        const auto v46 = powered_executor.execute(mission.simulation());
        if (!v46.powered_descent_active) {
            throw std::runtime_error("V0.9.46 baseline is not active");
        }

        trishula::PhysicalLunarTerminalDescentConfiguration terminal_configuration{};
        terminal_configuration.minimum_propellant_reserve_kg = 100.0;

        trishula::PhysicalLunarTerminalDescentExecutor terminal_executor(
            terminal_configuration,
            [phase](double t) { return moon_position(t, phase); },
            [phase](double t) { return moon_velocity(t, phase); },
            kMuMoon,
            kMoonRadius);

        const auto v47 = terminal_executor.execute(mission.simulation());
        if (!v47.terminal_descent_active) {
            throw std::runtime_error("V0.9.47 baseline is not active");
        }

        trishula::PhysicalLunarLandingIntegrationConfiguration landing_configuration{};
        landing_configuration.contact_start_altitude_meters = 50.0;
        landing_configuration.control_step_seconds = 0.005;
        landing_configuration.maximum_contact_duration_seconds = 30.0;
        landing_configuration.maximum_touchdown_speed_m_per_s = 20.0;
        landing_configuration.minimum_contact_legs = 3;

        trishula::PhysicalLunarLandingIntegrationExecutor landing_executor(
            landing_configuration,
            [phase](double t) { return moon_position(t, phase); },
            [phase](double t) { return moon_velocity(t, phase); },
            kMoonRadius);

        const auto result = landing_executor.execute(mission.simulation());

        const bool baseline = v47.terminal_descent_active;
        const bool boundary = result.valid_initial_state &&
            result.terminal_descent_boundary_reached;
        const bool touchdown = result.touchdown_detected;
        const bool stable = result.stable_contact;
        const bool landed = result.landed_state && !result.tip_over_detected;
        const bool physical_steps = result.physical_contact_steps > 0;

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.48 - Physical Landing / Surface Contact Integration\n";
        std::cout << "================================================================\n";
        std::cout << "  V0.9.47 baseline                 : " << (baseline ? "PASS" : "FAIL") << "\n";
        std::cout << "  initial altitude                  : " << result.initial_altitude_meters << " m\n";
        std::cout << "  initial vertical velocity         : " << result.initial_vertical_velocity_m_per_s << " m/s\n";
        std::cout << "  terminal boundary                 : " << (boundary ? "PASS" : "FAIL") << "\n";
        std::cout << "  touchdown detected                : " << (touchdown ? "PASS" : "FAIL") << "\n";
        std::cout << "  stable contact                    : " << (stable ? "PASS" : "FAIL") << "\n";
        std::cout << "  final altitude                    : " << result.final_altitude_meters << " m\n";
        std::cout << "  final vertical velocity           : " << result.final_vertical_velocity_m_per_s << " m/s\n";
        std::cout << "  touchdown speed                   : " << result.touchdown_speed_m_per_s << " m/s\n";
        std::cout << "  final tilt                        : " << result.final_tilt_rad << " rad\n";
        std::cout << "  final angular rate                : " << result.final_angular_rate_rad_s << " rad/s\n";
        std::cout << "  maximum contact force             : " << result.maximum_contact_force_newtons << " N\n";
        std::cout << "  maximum shock acceleration        : " << result.maximum_shock_acceleration_m_per_s2 << " m/s^2\n";
        std::cout << "  physical contact steps            : " << result.physical_contact_steps << "\n";
        std::cout << "  landed state                      : " << (landed ? "PASS" : "FAIL") << "\n";
        std::cout << "  physical surface contact          : " << (physical_steps ? "PASS" : "FAIL") << "\n";

        if (!(baseline && boundary && touchdown && stable && landed && physical_steps)) {
            throw std::runtime_error("V0.9.48 physical landing acceptance failed");
        }

        std::cout << "\nV0.9.48 physical landing / surface contact integration campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.48 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
