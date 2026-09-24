#include "trishula/ground/vehicle_command_adapter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace trishula {
namespace {

std::string uppercase_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::string trim_copy(std::string value) {
    const auto not_space = [](const unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

} // namespace

VehicleCommandAdapter::VehicleCommandAdapter(VehicleCommandAdapterConfiguration configuration)
    : configuration_(configuration),
      terrain_(configuration_.terrain_center_m),
      rover_(configuration_.rover_parameters) {
    if (configuration_.default_drive_dt_s <= 0.0) {
        throw std::invalid_argument("Vehicle adapter drive step must be positive");
    }
    snapshot_.target = "ROVER";
    snapshot_.mode = SurfaceRover::mode_name(rover_state_.mode);
}

const VehicleCommandAdapterConfiguration& VehicleCommandAdapter::configuration() const noexcept {
    return configuration_;
}

const VehicleAdapterSnapshot& VehicleCommandAdapter::snapshot() const noexcept {
    return snapshot_;
}

std::string VehicleCommandAdapter::normalize(const std::string& value) {
    return uppercase_copy(trim_copy(value));
}

bool VehicleCommandAdapter::has_parameter(const std::string& parameters, const std::string& key) {
    const auto expected = normalize(key) + "=";
    std::size_t start = 0U;
    while (start <= parameters.size()) {
        const auto end = parameters.find('|', start);
        const auto token = normalize(parameters.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (token.rfind(expected, 0U) == 0U) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
    return false;
}

bool VehicleCommandAdapter::parse_double_parameter(const std::string& parameters,
                                                   const std::string& key,
                                                   double& value) {
    const auto expected = normalize(key) + "=";
    std::size_t start = 0U;
    while (start <= parameters.size()) {
        const auto end = parameters.find('|', start);
        const auto raw_token = parameters.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const auto token = trim_copy(raw_token);
        const auto separator = token.find('=');
        if (separator != std::string::npos && normalize(token.substr(0, separator)) + "=" == expected) {
            try {
                const auto raw_value = trim_copy(token.substr(separator + 1U));
                std::size_t consumed = 0U;
                const auto parsed = std::stod(raw_value, &consumed);
                if (consumed != raw_value.size() || !std::isfinite(parsed)) {
                    return false;
                }
                value = parsed;
                return true;
            } catch (const std::exception&) {
                return false;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
    return false;
}

void VehicleCommandAdapter::sync_snapshot(const std::string& command_id, const std::string& opcode) {
    snapshot_.mode = SurfaceRover::mode_name(rover_state_.mode);
    snapshot_.x_m = rover_state_.x_m;
    snapshot_.y_m = rover_state_.y_m;
    snapshot_.heading_rad = rover_state_.heading_rad;
    snapshot_.speed_m_s = rover_state_.speed_m_s;
    snapshot_.battery_soc = rover_state_.battery_soc;
    snapshot_.deployed = rover_state_.deployed;
    snapshot_.mast_ready = rover_state_.mast_ready;
    snapshot_.localization_valid = rover_state_.localization_valid;
    snapshot_.drive_system_healthy = rover_state_.drive_system_healthy;
    snapshot_.last_command_id = command_id;
    snapshot_.last_opcode = opcode;
}

VehicleAdapterResult VehicleCommandAdapter::apply_unsupported_target(const GroundCommandRecord& command) {
    VehicleAdapterResult result{};
    result.status = VehicleAdapterStatus::UnsupportedTarget;
    result.message = "no physical vehicle adapter exists for target: " + normalize(command.target);
    result.snapshot = snapshot_;
    return result;
}

VehicleAdapterResult VehicleCommandAdapter::apply_rover(const GroundCommandRecord& command) {
    VehicleAdapterResult result{};
    result.snapshot = snapshot_;
    const auto opcode = normalize(command.opcode);

    if (opcode == "START_ROVER") {
        if (rover_state_.mode == RoverMode::Stowed) {
            if (!rover_.deploy_from_lander(rover_state_)) {
                result.status = VehicleAdapterStatus::PhysicalFault;
                result.message = "rover deployment failed physical preconditions";
                sync_snapshot(command.command_id, opcode);
                result.snapshot = snapshot_;
                return result;
            }
        }
        if (rover_state_.mode == RoverMode::Deployment) {
            if (!rover_.initialize_surface_systems(rover_state_)) {
                result.status = VehicleAdapterStatus::PhysicalFault;
                result.message = "rover surface initialization failed physical preconditions";
                sync_snapshot(command.command_id, opcode);
                result.snapshot = snapshot_;
                return result;
            }
        }
        sync_snapshot(command.command_id, opcode);
        result.status = VehicleAdapterStatus::Applied;
        result.message = "rover deployment and surface initialization applied";
        result.snapshot = snapshot_;
        return result;
    }

    if (opcode == "STOP_ROVER" || opcode == "STOP") {
        rover_state_.speed_m_s = 0.0;
        if (rover_state_.mode == RoverMode::Driving) {
            rover_state_.mode = RoverMode::SurfaceReady;
        }
        sync_snapshot(command.command_id, opcode);
        result.status = VehicleAdapterStatus::Applied;
        result.message = "rover motion stopped at physical interface";
        result.snapshot = snapshot_;
        return result;
    }

    if (opcode == "DRIVE") {
        if (!rover_state_.deployed || !rover_state_.mast_ready) {
            result.status = VehicleAdapterStatus::PreconditionsFailed;
            result.message = "rover must be deployed and surface-ready before DRIVE";
            return result;
        }

        double x = 0.0;
        double y = 0.0;
        double dt_s = configuration_.default_drive_dt_s;
        if (!parse_double_parameter(command.parameters, "x", x) ||
            !parse_double_parameter(command.parameters, "y", y)) {
            result.status = VehicleAdapterStatus::InvalidParameters;
            result.message = "DRIVE requires numeric x and y parameters";
            return result;
        }
        if (has_parameter(command.parameters, "dt") &&
            !parse_double_parameter(command.parameters, "dt", dt_s)) {
            result.status = VehicleAdapterStatus::InvalidParameters;
            result.message = "DRIVE dt parameter is invalid";
            return result;
        }
        if (dt_s <= 0.0) {
            result.status = VehicleAdapterStatus::InvalidParameters;
            result.message = "DRIVE dt must be positive";
            return result;
        }

        const RoverMissionTarget target{x, y, 1.5};
        result.rover_metrics = rover_.step_toward_waypoint(rover_state_, terrain_, target, dt_s);
        ++snapshot_.physical_steps;
        sync_snapshot(command.command_id, opcode);
        snapshot_.hazard_detected = result.rover_metrics.hazard_detected;
        result.snapshot = snapshot_;

        if (rover_state_.mode == RoverMode::Fault) {
            result.status = VehicleAdapterStatus::PhysicalFault;
            result.message = "rover physical model entered FAULT";
        } else if (rover_state_.mode == RoverMode::HazardHold) {
            result.status = VehicleAdapterStatus::PreconditionsFailed;
            result.message = "rover physical model entered HAZARD_HOLD";
        } else {
            result.status = VehicleAdapterStatus::Applied;
            result.message = "rover physical dynamics advanced by one command step";
        }
        return result;
    }

    result.status = VehicleAdapterStatus::UnsupportedOpcode;
    result.message = "opcode has no physical rover adapter mapping: " + opcode;
    result.snapshot = snapshot_;
    return result;
}

VehicleAdapterResult VehicleCommandAdapter::apply(const GroundCommandRecord& command,
                                                  const CommandExecutionResult& execution_result) {
    if (execution_result.status != CommandExecutionStatus::Executed) {
        VehicleAdapterResult result{};
        result.status = VehicleAdapterStatus::PreconditionsFailed;
        result.message = "logical command execution did not succeed: " + execution_result.message;
        result.snapshot = snapshot_;
        return result;
    }

    const auto target = normalize(command.target);
    if (target == "ROVER") {
        return apply_rover(command);
    }
    return apply_unsupported_target(command);
}

void VehicleCommandAdapter::reset() noexcept {
    rover_state_ = RoverState{};
    snapshot_ = VehicleAdapterSnapshot{};
    snapshot_.target = "ROVER";
    snapshot_.mode = SurfaceRover::mode_name(rover_state_.mode);
}

} // namespace trishula
