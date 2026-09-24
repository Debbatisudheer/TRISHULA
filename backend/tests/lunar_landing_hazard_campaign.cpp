#include "trishula/guidance/lunar_powered_descent.h"
#include "trishula/landing/lunar_terrain.h"

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

struct State {
    double altitude_m{30000.0};
    double x_m{-5000.0};
    double vertical_velocity_m_per_s{-50.0};
    double horizontal_velocity_m_per_s{200.0};
    double mass_kg{kInitialMass};
    double time_s{0.0};
};

void step(State& s, const trishula::LunarPoweredDescentCommand& command, double ground_elevation_m) {
    const double requested_accel = command.desired_acceleration_m_per_s2.magnitude();
    const double thrust = requested_accel * s.mass_kg;
    const double mdot = thrust / (kIsp * kG0);
    const double available = std::max(0.0, s.mass_kg - kDryMass);
    const double consumed = std::min(available, mdot * kDt);
    const double actual_mdot = consumed / kDt;
    const double actual_thrust = actual_mdot * kIsp * kG0;
    const double scale = thrust > 1e-12 ? actual_thrust / thrust : 0.0;
    const double ax = command.desired_acceleration_m_per_s2.x * scale;
    const double az = command.desired_acceleration_m_per_s2.y * scale;

    s.x_m += s.horizontal_velocity_m_per_s * kDt + 0.5 * ax * kDt * kDt;
    s.altitude_m += s.vertical_velocity_m_per_s * kDt + 0.5 * (az - kLunarGravity) * kDt * kDt;
    s.horizontal_velocity_m_per_s += ax * kDt;
    s.vertical_velocity_m_per_s += (az - kLunarGravity) * kDt;
    s.mass_kg -= consumed;
    s.time_s += kDt;

    (void)ground_elevation_m;
}

bool run() {
    trishula::LunarTerrainMap terrain(0.0);
    trishula::AutonomousLandingSiteSelector selector(terrain);
    const auto candidates = selector.evaluate(0.0, 2500.0, 100.0);
    const auto site = selector.selectBest(0.0, 2500.0, 100.0);

    // Force the site-selection system to demonstrate discrimination against hazards.
    bool saw_hazard = false;
    bool saw_safe = false;
    for (const auto& c : candidates) {
        saw_hazard |= !c.safe;
        saw_safe |= c.safe;
    }
    if (!saw_hazard || !saw_safe) throw std::runtime_error("terrain hazard classifier did not separate candidates");

    State state{};
    trishula::LunarPoweredDescentController controller(kThrust, kDryMass, kLunarGravity);
    for (int i = 0; i < 20000 && state.mass_kg > kDryMass + 1e-9; ++i) {
        const double ground = terrain.elevation(state.x_m);
        const double terrain_relative_altitude = state.altitude_m - ground;
        if (terrain_relative_altitude <= 0.0) break;
        const double altitude_measured = std::max(0.0, terrain_relative_altitude + 0.25 * std::sin(state.time_s * 0.09));
        const double x_measured = state.x_m + 0.4 * std::sin(state.time_s * 0.13);
        const double vv_measured = state.vertical_velocity_m_per_s + 0.01 * std::sin(state.time_s * 0.17);
        const double hv_measured = state.horizontal_velocity_m_per_s + 0.01 * std::cos(state.time_s * 0.19);
        const auto command = controller.computeToTarget(altitude_measured, x_measured, vv_measured,
                                                        hv_measured, state.mass_kg, site.x_m);
        step(state, command, ground);
        if (state.altitude_m <= terrain.elevation(state.x_m)) break;
    }

    const double final_ground = terrain.elevation(state.x_m);
    const double clearance = state.altitude_m - final_ground;
    const double touchdown_speed = std::hypot(state.horizontal_velocity_m_per_s, state.vertical_velocity_m_per_s);
    return std::abs(state.x_m - site.x_m) < 500.0 && touchdown_speed < 5.0 &&
           clearance <= 0.5 && site.safe && state.mass_kg > kDryMass;
}

} // namespace

int main() {
    try {
        trishula::LunarTerrainMap terrain(0.0);
        trishula::AutonomousLandingSiteSelector selector(terrain);
        const auto site = selector.selectBest(0.0, 2500.0, 100.0);
        const auto candidates = selector.evaluate(0.0, 2500.0, 100.0);
        const bool pass = run();
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.31 - Terrain-Relative Landing + Hazard Detection\n";
        std::cout << "==============================================================\n";
        std::cout << "  candidate sites evaluated       : " << candidates.size() << "\n";
        std::cout << "  selected landing x              : " << site.x_m << " m\n";
        std::cout << "  terrain elevation               : " << site.terrain_elevation_m << " m\n";
        std::cout << "  selected site slope             : " << site.slope << "\n";
        std::cout << "  selected site roughness         : " << site.roughness_m << " m\n";
        std::cout << "  hazard score                    : " << site.hazard_score << "\n";
        std::cout << "  crater                          : " << (site.crater ? "YES" : "NO") << "\n";
        std::cout << "  boulder field                   : " << (site.boulder_field ? "YES" : "NO") << "\n";
        std::cout << "  selected site SAFE              : " << (site.safe ? "YES" : "NO") << "\n";
        std::cout << "  terrain-relative guidance       : ENABLED\n";
        std::cout << "  autonomous landing-site select  : ENABLED\n";
        std::cout << "  hazard discrimination            : " << ((site.safe && candidates.size() > 10) ? "PASS" : "FAIL") << "\n";
        if (!pass) throw std::runtime_error("terrain-relative landing acceptance failed");
        std::cout << "  autonomous landing              : PASS\n";
        std::cout << "\nV0.9.31 terrain-relative landing + hazard detection campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.31 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
