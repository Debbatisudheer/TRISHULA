#include "trishula/mission/physical_mission_runtime_controller.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using Controller = trishula::PhysicalMissionRuntimeController;
using State = trishula::PhysicalMissionRuntimeState;

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const auto end = line.find('\t', start);
        out.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return out;
}

const char* state_name(const State state) {
    switch (state) {
    case State::Ready: return "READY";
    case State::Starting: return "STARTING";
    case State::Running: return "RUNNING";
    case State::Paused: return "PAUSED";
    case State::Resuming: return "RESUMING";
    case State::Completed: return "COMPLETED";
    case State::Aborted: return "ABORTED";
    case State::Fault: return "FAULT";
    }
    return "UNKNOWN";
}

const char* phase_name(const trishula::MissionPhase phase) {
    switch (phase) {
    case trishula::MissionPhase::Launch: return "Launch";
    case trishula::MissionPhase::EarthOrbit: return "Earth Orbit";
    case trishula::MissionPhase::TransLunarInjection: return "Trans-Lunar Injection";
    case trishula::MissionPhase::LunarCruise: return "Cruise to Moon";
    case trishula::MissionPhase::LunarOrbit: return "Lunar Orbit";
    case trishula::MissionPhase::Descent: return "Descent";
    case trishula::MissionPhase::Landing: return "Landing";
    case trishula::MissionPhase::RoverDeployment: return "Rover Deployment";
    case trishula::MissionPhase::SurfaceOperations: return "Surface Operations";
    case trishula::MissionPhase::Complete: return "Complete";
    case trishula::MissionPhase::Fault: return "Fault";
    }
    return "Unknown";
}

void emit(const Controller& controller, const char* result, const std::string& message = {}) {
    const auto& s = controller.snapshot();
    const auto& p = s.physical;
    std::cout << "RESULT\t" << result << '\t' << state_name(s.state) << '\t'
              << s.control_sequence << '\t' << p.telemetry_sequence << '\t'
              << std::setprecision(17) << p.mission_time_seconds << '\t'
              << p.phase_elapsed_seconds << '\t' << 0.0 << '\t'
              << p.position_x_m << '\t' << p.position_y_m << '\t' << p.position_z_m << '\t'
              << p.velocity_x_m_per_s << '\t' << p.velocity_y_m_per_s << '\t' << p.velocity_z_m_per_s << '\t'
              << p.altitude_m << '\t' << p.speed_m_per_s << '\t' << p.distance_to_moon_m << '\t'
              << p.moon_x_m << '\t' << p.moon_y_m << '\t' << phase_name(p.phase) << '\t'
              << p.battery_soc << '\t' << p.thermal_temperature_c << '\t' << p.thermal_margin << '\t'
              << p.propellant_fraction << '\t' << p.thrust_fraction << '\t'
              << (p.engine_firing ? "true" : "false") << '\t'
              << (p.rcs_firing ? "true" : "false") << '\t'
              << p.power_status << '\t' << p.propulsion_status << '\t'
              << p.thermal_status << '\t' << p.communication_status << '\t'
              << p.navigation_status << '\t' << p.health_status << '\t'
              << message << '\n' << std::flush;
}

} // namespace

int main() {
    try {
        trishula::PhysicalMissionExecutionConfiguration cfg{};
        cfg.event_driven_physical_arc = true;
        Controller controller(cfg);

        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            try {
                const auto fields = split_tab(line);
                const std::string command = fields.empty() ? "" : fields[0];
                if (command == "START") controller.start();
                else if (command == "PAUSE") controller.pause();
                else if (command == "RESUME") controller.resume();
                else if (command == "ABORT") controller.abort();
                else if (command == "RESET") controller.reset();
                else if (command == "STEP") controller.step();
                else if (command == "STATUS") {}
                else { emit(controller, "ERROR", "unsupported command"); continue; }
                emit(controller, "OK");
            } catch (const std::exception& ex) {
                emit(controller, "ERROR", ex.what());
            }
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "runtime bridge initialization failed: " << ex.what() << '\n';
        return 1;
    }
}
