#include "trishula/mission/physical_mission_execution.h"

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
        (void)mission.execute_lunar_orbit_insertion();
        (void)mission.execute_lunar_orbit_operations();
        const auto& prep = mission.execute_lunar_descent_preparation();
        if (!prep.descent_orbit_targeted || !prep.first_burn_executed || !prep.second_burn_executed ||
            !prep.descent_orbit_bounded || !prep.descent_ready) {
            throw std::runtime_error("physical lunar descent preparation acceptance failed");
        }

        std::cout << std::fixed << std::setprecision(6)
                  << "TRISHULA V0.9.45 - Physical Lunar Descent Preparation\n"
                  << "==============================================================\n"
                  << "  initial perilune altitude     : " << prep.initial_perilune_altitude_meters << " m\n"
                  << "  initial apolune altitude      : " << prep.initial_apolune_altitude_meters << " m\n"
                  << "  target perilune altitude      : " << prep.target_perilune_altitude_meters << " m\n"
                  << "  target apolune altitude       : " << prep.target_apolune_altitude_meters << " m\n"
                  << "  descent orbit targeting       : PHYSICAL\n"
                  << "  first burn planned delta-v    : " << prep.first_burn_planned_delta_v_m_per_s << " m/s\n"
                  << "  first burn executed delta-v  : " << prep.first_burn_executed_delta_v_m_per_s << " m/s\n"
                  << "  second burn planned delta-v   : " << prep.second_burn_planned_delta_v_m_per_s << " m/s\n"
                  << "  second burn executed delta-v : " << prep.second_burn_executed_delta_v_m_per_s << " m/s\n"
                  << "  total executed delta-v        : " << prep.total_executed_delta_v_m_per_s << " m/s\n"
                  << "  final perilune altitude       : " << prep.final_perilune_altitude_meters << " m\n"
                  << "  final apolune altitude        : " << prep.final_apolune_altitude_meters << " m\n"
                  << "  final semi-major axis         : " << prep.final_semi_major_axis_meters << " m\n"
                  << "  final eccentricity             : " << prep.final_eccentricity << "\n"
                  << "  propellant consumed            : " << prep.propellant_consumed_kg << " kg\n"
                  << "  descent orbit bounded          : YES\n"
                  << "  descent-ready state            : PASS\n"
                  << "  physical lunar descent prep    : PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.45 campaign FAILED: " << ex.what() << '\n';
        return 1;
    }
}
