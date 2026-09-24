#include "trishula/surface/post_landing_operations.h"

#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {

bool safe_surface_transition_test() {
    trishula::SurfaceLanderState state{};
    state.position_m = {0.0, 0.0, 0.0};
    state.velocity_m_s = {0.08, -0.05, -0.12};
    state.angular_velocity_rad_s = {0.02, 0.01, 0.015};
    state.tilt_rad = 0.05;
    state.mass_kg = 21800.0;
    state.landed_contact = true;
    state.landing_legs_locked = true;
    state.battery_soc = 0.90;
    state.thermal_margin = 0.85;

    trishula::PostLandingOperations ops{};
    const auto transition = ops.transition_to_surface_mode(state);
    const auto stable = ops.stabilize(state, 8.0, 0.1);
    return transition.surface_mode_entered && stable.attitude_stable &&
        state.mode == trishula::LanderMode::SurfaceSafe;
}

bool hardware_deployment_test() {
    trishula::SurfaceLanderState state{};
    state.landed_contact = true;
    state.landing_legs_locked = true;
    state.battery_soc = 0.9;
    state.thermal_margin = 0.9;
    state.mass_kg = 21800.0;
    trishula::PostLandingOperations ops{};
    const auto transition = ops.transition_to_surface_mode(state);
    const auto stabilized = ops.stabilize(state, 5.0);
    const auto metrics = ops.deploy_surface_hardware(state);
    if (!transition.surface_mode_entered || !stabilized.attitude_stable) {
        return false;
    }
    return metrics.antenna_deployed && metrics.camera_mast_deployed &&
        metrics.solar_array_deployed && state.mode == trishula::LanderMode::SurfaceOperations;
}

bool rover_interface_test() {
    trishula::SurfaceLanderState state{};
    state.landed_contact = true;
    state.landing_legs_locked = true;
    state.battery_soc = 0.9;
    state.thermal_margin = 0.9;
    state.mass_kg = 21800.0;
    trishula::PostLandingOperations ops{};
    const auto transition = ops.transition_to_surface_mode(state);
    const auto stabilized = ops.stabilize(state, 5.0);
    const auto deployed = ops.deploy_surface_hardware(state);
    if (!transition.surface_mode_entered || !stabilized.attitude_stable ||
        !deployed.antenna_deployed || !deployed.camera_mast_deployed || !deployed.solar_array_deployed) {
        return false;
    }
    const auto metrics = ops.prepare_rover_interface(state);
    return metrics.rover_interface_ready && state.mode == trishula::LanderMode::RoverInterfaceReady;
}

bool fault_inhibit_test() {
    trishula::SurfaceLanderState state{};
    state.landed_contact = true;
    state.landing_legs_locked = true;
    state.battery_soc = 0.10;
    state.thermal_margin = 0.9;
    state.mass_kg = 21800.0;
    trishula::PostLandingOperations ops{};
    const auto metrics = ops.transition_to_surface_mode(state);
    return metrics.fault_detected && state.mode == trishula::LanderMode::Fault;
}

} // namespace

int main() {
    try {
        const bool safe = safe_surface_transition_test();
        const bool deploy = hardware_deployment_test();
        const bool rover = rover_interface_test();
        const bool fault = fault_inhibit_test();

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.34 - Post-Landing Lander Operations\n";
        std::cout << "==============================================================\n";
        std::cout << "  safe surface transition       : " << (safe ? "PASS" : "FAIL") << "\n";
        std::cout << "  surface hardware deployment   : " << (deploy ? "PASS" : "FAIL") << "\n";
        std::cout << "  rover interface readiness     : " << (rover ? "PASS" : "FAIL") << "\n";
        std::cout << "  fault inhibition               : " << (fault ? "PASS" : "FAIL") << "\n";
        std::cout << "  flight-to-surface mode        : ENABLED\n";
        std::cout << "  landing hardware health gate  : ENABLED\n";
        std::cout << "  power/thermal health gate     : ENABLED\n";
        std::cout << "  antenna deployment             : ENABLED\n";
        std::cout << "  camera mast deployment         : ENABLED\n";
        std::cout << "  solar array deployment         : ENABLED\n";
        std::cout << "  rover interface gate           : ENABLED\n";
        std::cout << "  post-landing fault handling    : ENABLED\n";

        if (!(safe && deploy && rover && fault)) {
            throw std::runtime_error("post-landing operations acceptance failed");
        }

        std::cout << "\nV0.9.34 post-landing surface operations campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.34 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
