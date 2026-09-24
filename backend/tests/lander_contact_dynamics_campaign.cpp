#include "trishula/landing/lander_contact_dynamics.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double kPi = 3.141592653589793238462643383279502884;

trishula::Quaternion pitch_degrees(double degrees) {
    const double half = degrees * kPi / 180.0 * 0.5;
    return trishula::Quaternion{std::cos(half), 0.0, std::sin(half), 0.0};
}

bool safe_test() {
    trishula::LanderContactParameters parameters{};
    trishula::LanderContactDynamics dynamics(parameters);
    trishula::LanderContactState state{};
    state.position_m = {0.0, 0.0, 1.25};
    state.velocity_m_s = {0.05, -0.02, -0.8};
    state.attitude_body_to_inertial = pitch_degrees(0.5);
    state.angular_velocity_rad_s = {0.0, 0.005, 0.0};
    state.mass_kg = 21800.0;

    trishula::LanderContactMetrics last{};
    for (int i = 0; i < 800; ++i) {
        last = dynamics.step(state, 0.01);
    }

    const bool stable = last.landed_state;
    const bool upright = last.tilt_rad < 8.0 * kPi / 180.0;
    const bool low_rate = last.angular_rate_rad_s < 0.05;
    const bool low_vertical = std::abs(state.velocity_m_s.z) < 0.35;
    return stable && upright && low_rate && low_vertical;
}

bool tip_over_test() {
    trishula::LanderContactParameters parameters{};
    trishula::LanderContactDynamics dynamics(parameters);
    trishula::LanderContactState state{};
    state.position_m = {0.0, 0.0, 1.05};
    state.velocity_m_s = {0.0, 0.0, -0.4};
    state.attitude_body_to_inertial = pitch_degrees(22.0);
    state.mass_kg = 21800.0;

    trishula::LanderContactMetrics metrics{};
    for (int i = 0; i < 40; ++i) {
        metrics = dynamics.step(state, 0.01);
    }
    return metrics.tip_over_detected;
}

bool shock_metric_test() {
    trishula::LanderContactParameters parameters{};
    trishula::LanderContactDynamics dynamics(parameters);
    trishula::LanderContactState state{};
    state.position_m = {0.0, 0.0, 1.02};
    state.velocity_m_s = {0.0, 0.0, -4.0};
    state.mass_kg = 21800.0;

    double max_shock = 0.0;
    bool touchdown = false;
    for (int i = 0; i < 800; ++i) {
        const auto metrics = dynamics.step(state, 0.005);
        max_shock = std::max(max_shock, metrics.maximum_shock_acceleration_m_s2);
        touchdown = touchdown || metrics.touchdown_detected;
    }
    return touchdown && max_shock > 0.0;
}
}

int main() {
    try {
        const bool safe = safe_test();
        const bool tip = tip_over_test();
        const bool shock = shock_metric_test();

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.33 - Lander Contact + 6-DOF Hardware Dynamics\n";
        std::cout << "==============================================================\n";
        std::cout << "  safe touchdown scenario        : " << (safe ? "PASS" : "FAIL") << "\n";
        std::cout << "  tip-over detection scenario    : " << (tip ? "PASS" : "FAIL") << "\n";
        std::cout << "  touchdown shock metric         : " << (shock ? "PASS" : "FAIL") << "\n";
        std::cout << "  translational dynamics         : ENABLED\n";
        std::cout << "  rotational dynamics            : ENABLED\n";
        std::cout << "  landing-leg contact forces     : ENABLED\n";
        std::cout << "  friction model                 : ENABLED\n";
        std::cout << "  touchdown shock estimation     : ENABLED\n";
        std::cout << "  tip-over detection             : ENABLED\n";
        std::cout << "  landed-state determination     : ENABLED\n";

        if (!(safe && tip && shock)) {
            throw std::runtime_error("lander contact dynamics acceptance failed");
        }

        std::cout << "\nV0.9.33 lander contact + hardware dynamics campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.33 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
