#include "trishula/maneuver/trans_lunar_injection.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double kMuEarth = 3.986004418e14;
constexpr double kMuMoon = 4.9048695e12;
constexpr double kEarthRadius = 6.371e6;
constexpr double kMoonOrbitRadius = 384400.0e3;
constexpr double kMoonRadius = 1737.4e3;
constexpr double kParkingAltitude = 400.0e3;
constexpr double kLunarParkingAltitude = 100.0e3;
constexpr double kExpectedTliDvMin = 3000.0;
constexpr double kExpectedTliDvMax = 3200.0;
constexpr double kExpectedTofMinDays = 4.5;
constexpr double kExpectedTofMaxDays = 5.5;
constexpr double kExpectedC3Min = 5.5e5;
constexpr double kExpectedC3Max = 8.0e5;
constexpr double kExpectedPhaseMinDeg = 100.0;
constexpr double kExpectedPhaseMaxDeg = 130.0;
}

int main() {
    try {
        const double parking_radius = kEarthRadius + kParkingAltitude;
        const double lunar_perilune_radius = kMoonRadius + kLunarParkingAltitude;

        const trishula::TransLunarInjectionPlanner planner;
        const auto p = planner.plan(
            parking_radius,
            kMoonOrbitRadius,
            lunar_perilune_radius,
            kMuEarth,
            kMuMoon);

        const double tof_days = p.transfer_time_seconds / 86400.0;
        const double phase_deg = p.required_lunar_phase_angle_rad * 180.0 / 3.14159265358979323846;

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.23 - Trans-Lunar Injection Foundation\n";
        std::cout << "==============================================================\n";
        std::cout << "  parking orbit altitude              : " << kParkingAltitude << " m\n";
        std::cout << "  lunar orbit radius                  : " << kMoonOrbitRadius << " m\n";
        std::cout << "  transfer semi-major axis             : " << p.transfer_semi_major_axis_meters << " m\n";
        std::cout << "  parking circular speed               : " << p.parking_orbit_speed_m_per_s << " m/s\n";
        std::cout << "  TLI perigee speed                    : " << p.tli_perigee_speed_m_per_s << " m/s\n";
        std::cout << "  TLI delta-v                           : " << p.tli_delta_v_m_per_s << " m/s\n";
        std::cout << "  hyperbolic excess speed               : " << p.hyperbolic_excess_speed_m_per_s << " m/s\n";
        std::cout << "  C3                                     : " << p.characteristic_energy_c3_m2_per_s2 << " m^2/s^2\n";
        std::cout << "  transfer time                         : " << p.transfer_time_seconds << " s (" << tof_days << " days)\n";
        std::cout << "  required initial lunar phase          : " << phase_deg << " deg\n";
        std::cout << "  lunar sphere of influence             : " << p.lunar_soi_radius_meters / 1000.0 << " km\n";
        std::cout << "  lunar arrival speed @ 100 km          : " << p.lunar_arrival_speed_at_perilune_m_per_s << " m/s\n";
        std::cout << "  lunar circular speed @ 100 km         : " << p.lunar_circular_speed_at_perilune_m_per_s << " m/s\n";
        std::cout << "  nominal lunar orbit insertion dV      : " << p.nominal_lunar_orbit_insertion_delta_v_m_per_s << " m/s\n";

        if (!p.valid) throw std::runtime_error("TLI plan marked invalid");
        if (p.tli_delta_v_m_per_s < kExpectedTliDvMin || p.tli_delta_v_m_per_s > kExpectedTliDvMax) {
            throw std::runtime_error("Unexpected TLI delta-v");
        }
        if (tof_days < kExpectedTofMinDays || tof_days > kExpectedTofMaxDays) {
            throw std::runtime_error("Unexpected translunar transfer time");
        }
        if (p.characteristic_energy_c3_m2_per_s2 < kExpectedC3Min ||
            p.characteristic_energy_c3_m2_per_s2 > kExpectedC3Max) {
            throw std::runtime_error("Unexpected C3");
        }
        if (phase_deg < kExpectedPhaseMinDeg || phase_deg > kExpectedPhaseMaxDeg) {
            throw std::runtime_error("Unexpected lunar phase angle");
        }
        if (p.lunar_soi_radius_meters <= 0.0 ||
            p.lunar_arrival_speed_at_perilune_m_per_s <= p.lunar_circular_speed_at_perilune_m_per_s) {
            throw std::runtime_error("Invalid lunar arrival/circularization relationship");
        }
        if (p.nominal_lunar_orbit_insertion_delta_v_m_per_s < 0.0) {
            throw std::runtime_error("LOI delta-v must be non-negative");
        }

        std::cout << "\nV0.9.23 TLI foundation campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.23 TLI foundation campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
