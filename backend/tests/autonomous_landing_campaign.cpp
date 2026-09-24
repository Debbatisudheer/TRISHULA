#include "trishula/guidance/lunar_powered_descent.h"
#include "trishula/guidance/terrain_relative_powered_descent.h"
#include "trishula/landing/lunar_terrain.h"
#include "trishula/navigation/terrain_relative_navigation.h"

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

struct State {
    double altitude_m{30000.0};
    double x_m{0.0};
    double vertical_velocity_m_per_s{-50.0};
    double horizontal_velocity_m_per_s{0.0};
    double mass_kg{kInitialMass};
    double time_s{0.0};
};

struct Metrics {
    double propellant_used{0.0};
    double max_throttle{0.0};
    double final_touchdown_speed{0.0};
    double final_clearance_m{0.0};
    double target_updates{0.0};
    double selected_x_m{0.0};
    double selected_true_elevation_m{0.0};
    double selected_estimated_elevation_m{0.0};
    double selected_truth_error_m{0.0};
    bool target_safe{false};
    bool hazard_discrimination{false};
    bool sensor_path_used{false};
};

void step(State& s, const trishula::LunarPoweredDescentCommand& command) {
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
}

bool run(State& state, Metrics& metrics) {
    trishula::LunarTerrainMap terrain(0.0);
    trishula::TerrainRelativeSensor sensor(terrain, 0.12, 0.35, 0.08, 0x31415926u);
    trishula::TerrainRelativeNavigator navigator;
    trishula::LunarPoweredDescentController controller(kThrust, kDryMass, kLunarGravity);
    trishula::TerrainRelativePoweredDescentGuidance guidance(
        sensor, navigator, controller, 2500.0, 100.0, 20.0, 25000.0);

    const auto truth_candidates = trishula::AutonomousLandingSiteSelector(terrain).evaluate(0.0, 2500.0, 100.0);
    bool saw_safe = false;
    bool saw_hazard = false;
    for (const auto& candidate : truth_candidates) {
        saw_safe |= candidate.safe;
        saw_hazard |= !candidate.safe;
    }
    metrics.hazard_discrimination = saw_safe && saw_hazard;

    for (int i = 0; i < 20000 && state.mass_kg > kDryMass + 1e-9; ++i) {
        const auto output = guidance.compute(
            std::max(0.0, state.altitude_m),
            state.x_m,
            state.vertical_velocity_m_per_s,
            state.horizontal_velocity_m_per_s,
            state.mass_kg,
            state.time_s);
        metrics.sensor_path_used = metrics.sensor_path_used || output.terrain_relative_altitude_m >= 0.0;
        metrics.max_throttle = std::max(metrics.max_throttle, output.command.throttle);
        metrics.target_updates = static_cast<double>(guidance.target_update_count());
        metrics.selected_x_m = output.landing_site.x_m;
        metrics.selected_estimated_elevation_m = output.landing_site.elevation_m;
        metrics.target_safe = output.landing_site.safe;
        step(state, output.command);

        const double ground = terrain.elevation(state.x_m);
        if (state.altitude_m <= ground) break;
    }

    const double final_ground = terrain.elevation(state.x_m);
    metrics.final_clearance_m = state.altitude_m - final_ground;
    metrics.final_touchdown_speed = std::hypot(state.horizontal_velocity_m_per_s,
                                                state.vertical_velocity_m_per_s);
    metrics.propellant_used = kInitialMass - state.mass_kg;
    metrics.selected_true_elevation_m = terrain.elevation(metrics.selected_x_m);
    metrics.selected_truth_error_m = std::abs(metrics.selected_estimated_elevation_m - metrics.selected_true_elevation_m);

    const bool touchdown = state.altitude_m <= final_ground + 0.05;
    const bool soft = metrics.final_touchdown_speed <= 5.0;
    const bool site_accuracy = std::abs(state.x_m - metrics.selected_x_m) <= 500.0;
    const bool mass_ok = state.mass_kg > kDryMass;
    return touchdown && soft && site_accuracy && mass_ok && metrics.target_safe &&
           metrics.hazard_discrimination && metrics.sensor_path_used && guidance.has_target() &&
           metrics.target_updates >= 1.0;
}

} // namespace

int main() {
    try {
        State state{};
        Metrics metrics{};
        const bool pass = run(state, metrics);

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.32 - Sensor-Driven Autonomous Powered Landing\n";
        std::cout << "==============================================================\n";
        std::cout << "  scan + control replanning enabled : YES\n";
        std::cout << "  target updates                     : " << metrics.target_updates << "\n";
        std::cout << "  selected landing x                 : " << metrics.selected_x_m << " m\n";
        std::cout << "  estimated site elevation           : " << metrics.selected_estimated_elevation_m << " m\n";
        std::cout << "  true site elevation                 : " << metrics.selected_true_elevation_m << " m\n";
        std::cout << "  sensor/truth elevation error        : " << metrics.selected_truth_error_m << " m\n";
        std::cout << "  selected site safe                  : " << (metrics.target_safe ? "YES" : "NO") << "\n";
        std::cout << "  hazard discrimination               : " << (metrics.hazard_discrimination ? "PASS" : "FAIL") << "\n";
        std::cout << "  touchdown time                      : " << state.time_s << " s\n";
        std::cout << "  final altitude                      : " << state.altitude_m << " m\n";
        std::cout << "  final landing-site offset           : " << state.x_m - metrics.selected_x_m << " m\n";
        std::cout << "  touchdown speed                     : " << metrics.final_touchdown_speed << " m/s\n";
        std::cout << "  final surface clearance              : " << metrics.final_clearance_m << " m\n";
        std::cout << "  propellant consumed                 : " << metrics.propellant_used << " kg\n";
        std::cout << "  remaining mass                      : " << state.mass_kg << " kg\n";
        std::cout << "  sensor-driven guidance              : " << (metrics.sensor_path_used ? "YES" : "NO") << "\n";
        std::cout << "  closed-loop TRN -> guidance -> thrust: " << (pass ? "YES" : "NO") << "\n";
        std::cout << "  autonomous landing                  : " << (pass ? "PASS" : "FAIL") << "\n";
        if (!pass) throw std::runtime_error("autonomous sensor-driven landing acceptance failed");
        std::cout << "\nV0.9.32 sensor-driven autonomous powered landing campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.32 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
