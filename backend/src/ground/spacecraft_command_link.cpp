#include "trishula/ground/spacecraft_command_link.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace trishula {
namespace {

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        parts.push_back(value.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return parts;
}

} // namespace

GroundSpacecraftCommandLink::GroundSpacecraftCommandLink(SpacecraftCommandLinkConfiguration configuration)
    : configuration_(std::move(configuration)) {
    if (configuration_.maximum_propagation_seconds <= 0.0 ||
        configuration_.minimum_propagation_seconds <= 0.0 ||
        configuration_.minimum_propagation_seconds > configuration_.maximum_propagation_seconds) {
        throw std::invalid_argument("invalid spacecraft command propagation bounds");
    }
}

const SpacecraftCommandLinkConfiguration& GroundSpacecraftCommandLink::configuration() const noexcept {
    return configuration_;
}

std::uint32_t GroundSpacecraftCommandLink::last_sequence() const noexcept { return last_sequence_; }

std::string GroundSpacecraftCommandLink::normalize(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return out;
}

bool GroundSpacecraftCommandLink::parse_double_parameter(const std::string& parameters,
                                                          const std::string& key,
                                                          double& value) {
    for (const auto& item : split(parameters, '|')) {
        const auto equal = item.find('=');
        if (equal == std::string::npos || item.substr(0U, equal) != key) continue;
        try {
            std::size_t consumed = 0U;
            const double parsed = std::stod(item.substr(equal + 1U), &consumed);
            if (consumed != item.size() - equal - 1U || !std::isfinite(parsed)) return false;
            value = parsed;
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

bool GroundSpacecraftCommandLink::parse_vector_parameter(const std::string& parameters,
                                                           const char* prefix,
                                                           Vector3& value) {
    const std::string p(prefix);
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!parse_double_parameter(parameters, p + "_x", x) ||
        !parse_double_parameter(parameters, p + "_y", y) ||
        !parse_double_parameter(parameters, p + "_z", z)) {
        return false;
    }
    value = {x, y, z};
    return value.magnitude() > 1.0e-12;
}

std::string GroundSpacecraftCommandLink::status_name(const SpacecraftCommandStatus status) {
    switch (status) {
    case SpacecraftCommandStatus::Applied: return "APPLIED";
    case SpacecraftCommandStatus::InvalidTarget: return "INVALID_TARGET";
    case SpacecraftCommandStatus::InvalidOpcode: return "INVALID_OPCODE";
    case SpacecraftCommandStatus::InvalidParameters: return "INVALID_PARAMETERS";
    case SpacecraftCommandStatus::PreconditionsFailed: return "PRECONDITIONS_FAILED";
    case SpacecraftCommandStatus::PhysicalFault: return "PHYSICAL_FAULT";
    }
    return "UNKNOWN";
}

SpacecraftCommandResult GroundSpacecraftCommandLink::apply(const GroundCommandRecord& command,
                                                            SimulationEngine& simulation) {
    SpacecraftCommandResult result{};
    result.sequence_number = command.sequence_number;
    result.state_before = simulation.spacecraft().state();

    if (normalize(command.target) != normalize(configuration_.spacecraft_target)) {
        result.status = SpacecraftCommandStatus::InvalidTarget;
        result.message = "command target does not match spacecraft command link";
        return result;
    }
    if (command.sequence_number <= last_sequence_) {
        result.status = SpacecraftCommandStatus::PreconditionsFailed;
        result.message = "command sequence is not newer than the last applied sequence";
        return result;
    }

    const std::string opcode = normalize(command.opcode);
    PropulsionCommand propulsion{};
    double duration = 0.0;

    if (opcode == "COAST") {
        if (!parse_double_parameter(command.parameters, "dt", duration)) {
            result.status = SpacecraftCommandStatus::InvalidParameters;
            result.message = "COAST requires finite dt";
            return result;
        }
    } else if (opcode == "MAIN_ENGINE") {
        double throttle = 0.0;
        if (!parse_double_parameter(command.parameters, "throttle", throttle) ||
            throttle < 0.0 || throttle > 1.0 ||
            !parse_double_parameter(command.parameters, "dt", duration)) {
            result.status = SpacecraftCommandStatus::InvalidParameters;
            result.message = "MAIN_ENGINE requires throttle in [0,1] and finite dt";
            return result;
        }
        propulsion.main_engine_throttle = throttle;
        if (!parse_vector_parameter(command.parameters, "direction", propulsion.commanded_thrust_direction_body)) {
            result.status = SpacecraftCommandStatus::InvalidParameters;
            result.message = "MAIN_ENGINE requires non-zero direction_x/y/z";
            return result;
        }
    } else if (opcode == "RCS") {
        if (!parse_double_parameter(command.parameters, "dt", duration) ||
            !parse_vector_parameter(command.parameters, "force", propulsion.commanded_rcs_force_body_newtons)) {
            result.status = SpacecraftCommandStatus::InvalidParameters;
            result.message = "RCS requires dt and non-zero force_x/y/z";
            return result;
        }
    } else {
        result.status = SpacecraftCommandStatus::InvalidOpcode;
        result.message = "unsupported spacecraft command opcode: " + opcode;
        return result;
    }

    if (!std::isfinite(duration) || duration < configuration_.minimum_propagation_seconds ||
        duration > configuration_.maximum_propagation_seconds) {
        result.status = SpacecraftCommandStatus::InvalidParameters;
        result.message = "propagation duration outside configured physical bounds";
        return result;
    }

    try {
        simulation.step_for_duration(propulsion, duration);
    } catch (const std::exception& error) {
        result.status = SpacecraftCommandStatus::PhysicalFault;
        result.message = error.what();
        return result;
    }

    last_sequence_ = command.sequence_number;
    result.status = SpacecraftCommandStatus::Applied;
    result.message = "spacecraft command applied through physical simulation";
    result.propagated_seconds = duration;
    result.state_after = simulation.spacecraft().state();
    result.propulsion = simulation.last_propulsion_output();
    (void)status_name;
    return result;
}

void GroundSpacecraftCommandLink::reset() noexcept { last_sequence_ = 0U; }

} // namespace trishula
