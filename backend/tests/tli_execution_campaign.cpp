#include "trishula/core/simulation_engine.h"
#include "trishula/maneuver/trans_lunar_injection.h"
#include "trishula/navigation/navigation.h"
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

double finite_burn_duration(double delta_v) {
    const double final_mass = kInitialMass * std::exp(-delta_v / (kIsp * kG0));
    if (final_mass <= kDryMass) throw std::runtime_error("TLI requires more propellant than available");
    const double mdot = kThrust / (kIsp * kG0);
    return (kInitialMass - final_mass) / mdot;
}
}

int main() {
    try {
        const double parking_radius = kEarthRadius + kParkingAltitude;
        const double lunar_perilune_radius = kMoonRadius + kLunarParkingAltitude;
        trishula::TransLunarInjectionPlan plan = trishula::TransLunarInjectionPlanner{}.plan(
            parking_radius, kMoonOrbitRadius, lunar_perilune_radius, kMuEarth, kMuMoon);

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

        const double burn_duration = finite_burn_duration(plan.tli_delta_v_m_per_s);
        double remaining = burn_duration;
        std::size_t burn_steps = 0;
        while (remaining > 1e-12) {
            const double dt = std::min(kStep, remaining);
            const auto state = sim.spacecraft().state();
            const trishula::Vector3 r = state.position_meters;
            const trishula::Vector3 radial = r.normalized();
            const trishula::Vector3 tangential = state.velocity_m_per_s - radial * state.velocity_m_per_s.dot(radial);
            const trishula::Vector3 inertial_direction = tangential.normalized();
            const trishula::Vector3 body_direction = state.attitude_body_to_inertial.inverse().rotate(inertial_direction);
            trishula::PropulsionCommand cmd{};
            cmd.main_engine_throttle = 1.0;
            cmd.commanded_thrust_direction_body = body_direction;
            sim.step_for_duration(cmd, dt);
            remaining -= dt;
            ++burn_steps;
        }

        const double achieved_dv = plan.parking_orbit_speed_m_per_s;
        (void)achieved_dv;
        const auto post_burn = trishula::EarthCenteredNavigator{}.determine(
            trishula::EstimatedState{sim.clock().time(), sim.spacecraft().state().position_meters,
                                     sim.spacecraft().state().velocity_m_per_s,
                                     sim.spacecraft().state().acceleration_m_per_s2,
                                     sim.spacecraft().state().attitude_body_to_inertial,
                                     sim.spacecraft().state().angular_velocity_rad_s, 0.0},
            earth);

        double min_moon_distance = 1.0e99;
        double min_moon_time = 0.0;
        bool soi_entry = false;
        double soi_entry_time = 0.0;
        const double max_coast = plan.transfer_time_seconds + 6.0 * 86400.0;
        while (sim.clock().time() < max_coast) {
            sim.step({});
            const double t = sim.clock().time();
            const auto &state = sim.spacecraft().state();
            const trishula::Vector3 moon = moon_position(plan.required_lunar_phase_angle_rad, t);
            const double distance = (state.position_meters - moon).magnitude();
            const double soi = plan.lunar_soi_radius_meters;
            if (distance < min_moon_distance) {
                min_moon_distance = distance;
                min_moon_time = t;
            }
            if (!soi_entry && distance <= soi) {
                soi_entry = true;
                soi_entry_time = t;
                break;
            }
        }

        const double elapsed_days = sim.clock().time() / 86400.0;
        const auto final_state = sim.spacecraft().state();
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.24 - Finite-Duration TLI + Earth-Moon Coast\n";
        std::cout << "==============================================================\n";
        std::cout << "  parking orbit altitude          : " << kParkingAltitude << " m\n";
        std::cout << "  planned TLI dV                  : " << plan.tli_delta_v_m_per_s << " m/s\n";
        std::cout << "  finite TLI burn duration        : " << burn_duration << " s\n";
        std::cout << "  burn integration steps          : " << burn_steps << "\n";
        std::cout << "  post-burn Earth speed            : " << post_burn.speed_m_per_s << " m/s\n";
        std::cout << "  post-burn C3                     : " << (post_burn.orbit.specific_orbital_energy_j_per_kg * 2.0) << " m^2/s^2\n";
        std::cout << "  coast elapsed                    : " << elapsed_days << " days\n";
        std::cout << "  lunar SOI radius                 : " << plan.lunar_soi_radius_meters / 1000.0 << " km\n";
        std::cout << "  closest Moon distance            : " << min_moon_distance / 1000.0 << " km\n";
        std::cout << "  closest-approach time             : " << min_moon_time / 86400.0 << " days\n";
        std::cout << "  lunar SOI entry                  : " << (soi_entry ? "YES" : "NO") << "\n";
        if (soi_entry) std::cout << "  lunar SOI entry time              : " << soi_entry_time / 86400.0 << " days\n";
        std::cout << "  final Earth distance              : " << final_state.position_meters.magnitude() / 1000.0 << " km\n";
        std::cout << "  final spacecraft mass             : " << final_state.mass_kg << " kg\n";

        if (!soi_entry) throw std::runtime_error("Spacecraft did not enter the simplified lunar sphere of influence");
        if (min_moon_distance > plan.lunar_soi_radius_meters) throw std::runtime_error("Closest approach remained outside lunar SOI");
        if (final_state.mass_kg <= kDryMass) throw std::runtime_error("Finite TLI exhausted dry mass");

        std::cout << "\nV0.9.24 finite-duration TLI + Earth-Moon coast campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.24 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
