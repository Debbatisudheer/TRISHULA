#include "trishula/guidance/lunar_powered_descent.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double kLunarGravity = 1.62;
constexpr double kDryMass = 20000.0;
constexpr double kInitialMass = 30000.0;
constexpr double kThrust = 110000.0;
constexpr double kIsp = 320.0;
constexpr double kG0 = 9.80665;
constexpr double kDt = 0.05;
constexpr double kInitialAltitude = 30000.0;
constexpr double kInitialX = -5000.0;
constexpr double kInitialHorizontalVelocity = 200.0;
constexpr double kInitialVerticalVelocity = -50.0;

struct State {
    double altitude_m{kInitialAltitude};
    double x_m{kInitialX};
    double vertical_velocity_m_per_s{kInitialVerticalVelocity};
    double horizontal_velocity_m_per_s{kInitialHorizontalVelocity};
    double mass_kg{kInitialMass};
    double time_s{0.0};
};

struct Metrics {
    double max_throttle{0.0};
    double max_speed{0.0};
    double propellant_used{0.0};
    double max_altitude_error{0.0};
};

void step(State& s, const trishula::LunarPoweredDescentCommand& command, Metrics& metrics) {
    const double acceleration = command.desired_acceleration_m_per_s2.magnitude();
    const double thrust = acceleration * s.mass_kg;
    const double mass_flow = thrust / (kIsp * kG0);
    const double available = std::max(0.0, s.mass_kg - kDryMass);
    const double consumed = std::min(available, mass_flow * kDt);
    const double actual_mass_flow = consumed / kDt;
    const double actual_thrust = actual_mass_flow * kIsp * kG0;
    const double actual_accel_scale = thrust > 0.0 ? actual_thrust / thrust : 0.0;
    const double ax = command.desired_acceleration_m_per_s2.x * actual_accel_scale;
    const double az = command.desired_acceleration_m_per_s2.y * actual_accel_scale;

    s.x_m += s.horizontal_velocity_m_per_s * kDt + 0.5 * ax * kDt * kDt;
    s.altitude_m += s.vertical_velocity_m_per_s * kDt + 0.5 * (az - kLunarGravity) * kDt * kDt;
    s.horizontal_velocity_m_per_s += ax * kDt;
    s.vertical_velocity_m_per_s += (az - kLunarGravity) * kDt;
    s.mass_kg -= consumed;
    s.time_s += kDt;

    metrics.max_throttle = std::max(metrics.max_throttle, command.throttle);
    metrics.max_speed = std::max(metrics.max_speed,
        std::hypot(s.horizontal_velocity_m_per_s, s.vertical_velocity_m_per_s));
    metrics.propellant_used = kInitialMass - s.mass_kg;
    metrics.max_altitude_error = std::max(metrics.max_altitude_error, std::abs(s.altitude_m));
}

bool run_campaign(State& state, Metrics& metrics) {
    trishula::LunarPoweredDescentController controller(kThrust, kDryMass, kLunarGravity);
    for (int i = 0; i < 20000 && state.altitude_m > 0.0 && state.mass_kg > kDryMass + 1e-9; ++i) {
        // Deterministic sensor emulation: small bounded measurement errors.
        const double altitude_measured = std::max(0.0, state.altitude_m + 0.35 * std::sin(state.time_s * 0.07));
        const double x_measured = state.x_m + 0.5 * std::sin(state.time_s * 0.11);
        const double vv_measured = state.vertical_velocity_m_per_s + 0.01 * std::sin(state.time_s * 0.13);
        const double hv_measured = state.horizontal_velocity_m_per_s + 0.01 * std::cos(state.time_s * 0.17);
        const auto command = controller.compute(altitude_measured, x_measured, vv_measured, hv_measured, state.mass_kg);
        step(state, command, metrics);
    }
    const double touchdown_speed = std::hypot(state.horizontal_velocity_m_per_s, state.vertical_velocity_m_per_s);
    const bool touchdown = state.altitude_m <= 0.0;
    const bool soft_landing = touchdown && touchdown_speed <= 5.0 && std::abs(state.x_m) <= 500.0;
    const bool mass_ok = state.mass_kg > kDryMass;
    const bool powered_descent_exercised = metrics.max_throttle > 0.5;
    return soft_landing && mass_ok && powered_descent_exercised;
}

} // namespace

int main() {
    try {
        State state{};
        Metrics metrics{};
        const bool pass = run_campaign(state, metrics);
        const double touchdown_speed = std::hypot(state.horizontal_velocity_m_per_s, state.vertical_velocity_m_per_s);

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.29 - Powered Lunar Descent\n";
        std::cout << "==============================================================\n";
        std::cout << "  initial altitude                 : " << kInitialAltitude << " m\n";
        std::cout << "  initial horizontal velocity      : " << kInitialHorizontalVelocity << " m/s\n";
        std::cout << "  initial vertical velocity        : " << kInitialVerticalVelocity << " m/s\n";
        std::cout << "  descent engine maximum thrust    : " << kThrust << " N\n";
        std::cout << "  final touchdown time             : " << state.time_s << " s\n";
        std::cout << "  final altitude                   : " << state.altitude_m << " m\n";
        std::cout << "  final landing-site offset        : " << state.x_m << " m\n";
        std::cout << "  final horizontal velocity        : " << state.horizontal_velocity_m_per_s << " m/s\n";
        std::cout << "  final vertical velocity          : " << state.vertical_velocity_m_per_s << " m/s\n";
        std::cout << "  final touchdown speed            : " << touchdown_speed << " m/s\n";
        std::cout << "  propellant consumed              : " << metrics.propellant_used << " kg\n";
        std::cout << "  remaining mass                   : " << state.mass_kg << " kg\n";
        std::cout << "  maximum throttle                 : " << metrics.max_throttle << "\n";
        std::cout << "  closed-loop powered descent      : " << (metrics.max_throttle > 0.5 ? "YES" : "NO") << "\n";
        std::cout << "  soft landing                     : " << (pass ? "YES" : "NO") << "\n";
        std::cout << "  landing-site accuracy            : " << (std::abs(state.x_m) <= 500.0 ? "PASS" : "FAIL") << "\n";
        if (!pass) throw std::runtime_error("powered descent acceptance failed");
        std::cout << "\nV0.9.29 powered lunar descent campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.29 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
