#include "trishula/mission/physical_mission_execution.h"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        trishula::PhysicalMissionExecutionConfiguration cfg{};
        cfg.step_seconds = 1.0;
        cfg.earth_orbit_hold_seconds = 10.0;
        cfg.event_driven_physical_arc = true;
        trishula::PhysicalMissionExecutionEngine mission(cfg);
        mission.run_until_phase(trishula::MissionPhase::LunarOrbit, 1000000U);
        const auto& approach = mission.snapshot();
        if (approach.phase != trishula::MissionPhase::LunarOrbit) throw std::runtime_error("did not reach lunar SOI");
        const auto& loi = mission.execute_lunar_orbit_insertion();
        if (loi.post_burn_specific_energy_j_per_kg >= 0.0) throw std::runtime_error("post-LOI orbit is not bound");
        const double reported_vinf = std::sqrt(std::max(0.0, loi.plan.pre_burn_speed_m_per_s * loi.plan.pre_burn_speed_m_per_s - 2.0 * loi.plan.moon_gravitational_parameter_m3_s2 / (loi.plan.moon_radius_meters + loi.plan.target_perilune_altitude_meters)));
        if (!loi.orbit_captured || std::abs(loi.post_burn_perilune_altitude_meters - 100000.0) > 20000.0 || std::abs(loi.post_burn_apolune_altitude_meters - 1000000.0) > 20000.0) throw std::runtime_error("integrated LOI orbit target acceptance failed");
        std::cout << std::fixed << std::setprecision(6)
                  << "TRISHULA V0.9.43 - Integrated Physical Earth-Moon + LOI\n"
                  << "==============================================================\n"
                  << "  Earth-Moon approach            : PHYSICAL\n"
                  << "  lunar SOI arrival              : DETECTED\n"
                  << "  perilune reached               : PHYSICAL\n"
                  << "  incoming lunar v-infinity      : " << reported_vinf << " m/s\n"
                  << "  LOI guidance target altitude  : " << loi.plan.target_perilune_altitude_meters << " m\n"
                  << "  perilune radius               : " << loi.perilune_radius_meters << " m\n"
                  << "  planned LOI delta-v            : " << loi.plan.planned_delta_v_m_per_s << " m/s\n"
                  << "  mid-course correction dv       : " << loi.midcourse_correction_delta_v_m_per_s << " m/s\n"
                  << "  finite LOI burn                : EXECUTED\n"
                  << "  achieved delta-v               : " << loi.achieved_delta_v_m_per_s << " m/s\n"
                  << "  burn integration steps         : " << loi.burn_integration_steps << "\n"
                  << "  captured specific energy       : " << loi.post_burn_specific_energy_j_per_kg << " J/kg\n"
                  << "  captured semi-major axis       : " << loi.post_burn_semi_major_axis_meters << " m\n"
                  << "  captured eccentricity          : " << loi.post_burn_eccentricity << "\n"
                  << "  captured perilune altitude     : " << loi.post_burn_perilune_altitude_meters << " m\n"
                  << "  captured apolune altitude      : " << loi.post_burn_apolune_altitude_meters << " m\n"
                  << "  propellant consumed           : " << loi.propellant_consumed_kg << " kg\n"
                  << "  lunar orbit capture            : PASS\n"
                  << "  integrated Earth-Moon → LOI    : PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.43 campaign FAILED: " << ex.what() << '\n';
        return 1;
    }
}
