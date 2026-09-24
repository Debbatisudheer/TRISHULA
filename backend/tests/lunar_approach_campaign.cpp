#include "trishula/core/simulation_engine.h"
#include "trishula/maneuver/trans_lunar_injection.h"
#include "trishula/navigation/lunar_navigation.h"
#include "trishula/physics/perturbations.h"
#include "trishula/propulsion/propulsion.h"
#include "trishula/vehicle/spacecraft.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kMuEarth = 3.986004418e14;
constexpr double kMuMoon = 4.9048695e12;
constexpr double kEarthRadius = 6.371e6;
constexpr double kMoonRadius = 1.7374e6;
constexpr double kMoonOrbitRadius = 384400.0e3;
constexpr double kParkingAltitude = 400.0e3;
constexpr double kLunarParkingAltitude = 100.0e3;
constexpr double kInitialMass = 50000.0;
constexpr double kDryMass = 20000.0;
constexpr double kThrust = 5.0e6;
constexpr double kIsp = 450.0;
constexpr double kStep = 1.0;
constexpr double kG0 = 9.80665;

trishula::Vector3 moon_position(double phase, double time_s) {
    const double omega = 2.0 * kPi / (27.321661 * 86400.0);
    const double theta = phase + omega * time_s;
    return {kMoonOrbitRadius * std::cos(theta), kMoonOrbitRadius * std::sin(theta), 0.0};
}

double burn_duration_for_delta_v(double delta_v) {
    const double final_mass = kInitialMass * std::exp(-delta_v / (kIsp * kG0));
    if (final_mass <= kDryMass) throw std::runtime_error("TLI requires more propellant than available");
    const double mdot = kThrust / (kIsp * kG0);
    return (kInitialMass - final_mass) / mdot;
}

trishula::LunarNavigationState lunar_state_at(
    const trishula::SimulationEngine& sim,
    const trishula::TransLunarInjectionPlan& plan) {
    return trishula::LunarNavigator{}.determine(
        sim.clock().time(),
        sim.spacecraft().state().position_meters,
        sim.spacecraft().state().velocity_m_per_s,
        kMuMoon,
        kMoonRadius,
        kMoonOrbitRadius,
        27.321661 * 86400.0,
        plan.required_lunar_phase_angle_rad);
}
}

int main() {
    try {
        const double parking_radius = kEarthRadius + kParkingAltitude;
        const double lunar_perilune_radius = kMoonRadius + kLunarParkingAltitude;
        const auto plan = trishula::TransLunarInjectionPlanner{}.plan(
            parking_radius, kMoonOrbitRadius, lunar_perilune_radius, kMuEarth, kMuMoon);

        // Physical consistency check: the Earth-centered TLI transfer is elliptical,
        // therefore its Earth C3 must be negative. The previous label "hyperbolic excess"
        // belongs to the Moon-relative arrival speed, not Earth departure C3.
        const double earth_transfer_energy =
            0.5 * std::pow(plan.parking_orbit_speed_m_per_s + plan.tli_delta_v_m_per_s, 2.0) -
            kMuEarth / parking_radius;
        if (earth_transfer_energy >= 0.0) {
            throw std::runtime_error("Earth-centered TLI transfer is expected to remain elliptic");
        }
        const double expected_transfer_c3 = 2.0 * earth_transfer_energy;
        if (std::abs(expected_transfer_c3 + 2.027143964350e6) > 2.0e4) {
            throw std::runtime_error("Unexpected Earth transfer C3 consistency result");
        }

        trishula::TrueState initial{};
        initial.position_meters = {parking_radius, 0.0, 0.0};
        initial.velocity_m_per_s = {0.0, plan.parking_orbit_speed_m_per_s, 0.0};
        initial.mass_kg = kInitialMass;

        trishula::Spacecraft spacecraft(initial, {1.0e7, 1.0e7, 1.0e7});
        trishula::CelestialBody earth(kMuEarth, kEarthRadius);
        trishula::MainEngine engine(kThrust, kIsp, kDryMass);
        trishula::RcsModule rcs(100.0, 120.0, kDryMass);
        trishula::SensorConfiguration sensors{};
        sensors.random_seed = 0x5A17C0DEULL;
        trishula::PerturbationConfiguration perturbations{};
        perturbations.enable_third_body_moon = true;
        perturbations.moon_phase_rad = plan.required_lunar_phase_angle_rad;
        trishula::SimulationEngine sim(
            earth, spacecraft, kStep, engine, rcs, trishula::SensorSuite(sensors), perturbations);

        const double burn_duration = burn_duration_for_delta_v(plan.tli_delta_v_m_per_s);
        double remaining = burn_duration;
        while (remaining > 1e-12) {
            const double dt = std::min(kStep, remaining);
            const auto state = sim.spacecraft().state();
            const auto radial = state.position_meters.normalized();
            const auto tangential = state.velocity_m_per_s - radial * state.velocity_m_per_s.dot(radial);
            const auto inertial_direction = tangential.normalized();
            const auto body_direction = state.attitude_body_to_inertial.inverse().rotate(inertial_direction);
            trishula::PropulsionCommand command{};
            command.main_engine_throttle = 1.0;
            command.commanded_thrust_direction_body = body_direction;
            sim.step_for_duration(command, dt);
            remaining -= dt;
        }

        const double max_coast = plan.transfer_time_seconds + 6.0 * 86400.0;
        const double soi = plan.lunar_soi_radius_meters;
        double closest_distance = 1.0e99;
        double closest_time = 0.0;
        bool entered_soi = false;
        double entry_time = 0.0;

        while (sim.clock().time() < max_coast) {
            sim.step({});
            const auto nav = lunar_state_at(sim, plan);
            if (nav.moon_distance_meters < closest_distance) {
                closest_distance = nav.moon_distance_meters;
                closest_time = sim.clock().time();
            }
            if (nav.moon_distance_meters <= soi) {
                entered_soi = true;
                entry_time = sim.clock().time();
                break;
            }
        }

        if (!entered_soi) throw std::runtime_error("Spacecraft did not enter lunar SOI");
        const auto approach = lunar_state_at(sim, plan);
        if (approach.moon_distance_meters > soi + 1000.0) {
            throw std::runtime_error("Lunar approach state is not inside SOI");
        }
        if (approach.altitude_above_moon_surface_meters <= 0.0) {
            throw std::runtime_error("Spacecraft intersects simplified Moon surface");
        }
        if (approach.relative_speed_m_per_s <= 0.0) {
            throw std::runtime_error("Invalid Moon-relative speed");
        }
        // The simplified SOI crossing is a geometric event, not a guaranteed lunar
        // hyperbolic-approach state. Report the Moon-relative energy explicitly and
        // never use the Earth-centered C3 as a substitute for it.
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.25 - Lunar SOI Entry + Moon-Relative Navigation\n";
        std::cout << "==============================================================\n";
        std::cout << "  earth transfer C3                  : " << expected_transfer_c3 << " m^2/s^2\n";
        std::cout << "  Earth transfer classification      : " << (expected_transfer_c3 < 0.0 ? "ELLIPTIC" : "HYPERBOLIC") << "\n";
        std::cout << "  TLI burn duration                  : " << burn_duration << " s\n";
        std::cout << "  lunar SOI radius                   : " << soi / 1000.0 << " km\n";
        std::cout << "  SOI entry time                     : " << entry_time / 86400.0 << " days\n";
        std::cout << "  closest Moon distance              : " << closest_distance / 1000.0 << " km\n";
        std::cout << "  closest-approach time              : " << closest_time / 86400.0 << " days\n";
        std::cout << "  Moon-relative distance             : " << approach.moon_distance_meters / 1000.0 << " km\n";
        std::cout << "  Moon-relative altitude             : " << approach.altitude_above_moon_surface_meters / 1000.0 << " km\n";
        std::cout << "  Moon-relative speed                : " << approach.relative_speed_m_per_s << " m/s\n";
        std::cout << "  Moon-relative radial velocity      : " << approach.radial_velocity_m_per_s << " m/s\n";
        std::cout << "  Moon-relative flight-path angle    : " << approach.flight_path_angle_rad * 180.0 / kPi << " deg\n";
        std::cout << "  lunar specific energy              : " << approach.specific_orbital_energy_j_per_kg << " J/kg\n";
        std::cout << "  lunar C3                            : " << approach.characteristic_energy_c3_m2_per_s2 << " m^2/s^2\n";
        std::cout << "  lunar hyperbolic excess speed      : " << approach.hyperbolic_excess_speed_m_per_s << " m/s\n";
        std::cout << "  lunar approach classification      : " << (approach.hyperbolic_approach ? "HYPERBOLIC" : "ELLIPTIC/CAPTURED") << "\n";
        std::cout << "  spacecraft mass at SOI             : " << sim.spacecraft().state().mass_kg << " kg\n";
        std::cout << "\nV0.9.25 lunar SOI + Moon-relative navigation campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.25 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
