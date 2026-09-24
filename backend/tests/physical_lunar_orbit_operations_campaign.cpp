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
        const auto& ops = mission.execute_lunar_orbit_operations();
        if (!ops.orbit_bounded || !ops.repeated_apsides_detected || !ops.orbit_stable || !ops.station_keeping_effective) {
            throw std::runtime_error("physical lunar orbit operations acceptance failed");
        }
        std::cout << std::fixed << std::setprecision(6)
                  << "TRISHULA V0.9.44 - Physical Lunar Orbit Operations\n"
                  << "==============================================================\n"
                  << "  post-LOI lunar orbit           : PHYSICAL\n"
                  << "  revolutions simulated          : " << ops.revolutions_completed << "\n"
                  << "  measured orbital period        : " << ops.orbital_period_seconds << " s\n"
                  << "  baseline semi-major axis       : " << ops.baseline_semi_major_axis_meters << " m\n"
                  << "  baseline eccentricity          : " << ops.baseline_eccentricity << "\n"
                  << "  baseline perilune altitude     : " << ops.baseline_perilune_altitude_meters << " m\n"
                  << "  baseline apolune altitude      : " << ops.baseline_apolune_altitude_meters << " m\n"
                  << "  repeated apsis detection       : YES\n"
                  << "  bounded lunar orbit             : YES\n"
                  << "  station-keeping disturbance    : -" << ops.disturbance_delta_v_m_per_s << " m/s\n"
                  << "  disturbed apolune altitude     : " << ops.disturbed_apolune_altitude_meters << " m\n"
                  << "  computed correction delta-v    : " << ops.correction_delta_v_m_per_s << " m/s\n"
                  << "  corrected apolune altitude     : " << ops.corrected_apolune_altitude_meters << " m\n"
                  << "  station-keeping correction     : EFFECTIVE\n"
                  << "  final semi-major axis          : " << ops.final_semi_major_axis_meters << " m\n"
                  << "  final eccentricity              : " << ops.final_eccentricity << "\n"
                  << "  final perilune altitude        : " << ops.final_perilune_altitude_meters << " m\n"
                  << "  final apolune altitude         : " << ops.final_apolune_altitude_meters << " m\n"
                  << "  final apolune error             : " << ops.final_apolune_error_meters << " m\n"
                  << "  lunar orbit stability           : PASS\n"
                  << "  physical lunar orbit operations : PASS\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.44 campaign FAILED: " << ex.what() << '\n';
        return 1;
    }
}
