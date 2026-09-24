#include "trishula/ground/command_execution.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace trishula {
namespace {

std::string uppercase_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return value;
}

std::string make_payload(const std::initializer_list<std::pair<std::string, std::string>>& fields) {
    std::ostringstream out;
    bool first = true;
    for (const auto& [key, value] : fields) {
        if (!first) out << '|';
        first = false;
        out << key << '=' << value;
    }
    return out.str();
}

} // namespace

GroundCommandExecutionEngine::GroundCommandExecutionEngine(CommandExecutionConfiguration configuration)
    : configuration_(configuration),
      next_event_sequence_(static_cast<std::uint32_t>(configuration_.event_sequence_start)),
      next_telemetry_sequence_(static_cast<std::uint32_t>(configuration_.telemetry_sequence_start)),
      next_acknowledgement_sequence_(static_cast<std::uint32_t>(configuration_.acknowledgement_sequence_start)) {}

const CommandExecutionConfiguration& GroundCommandExecutionEngine::configuration() const noexcept {
    return configuration_;
}

const CommandExecutionSnapshot* GroundCommandExecutionEngine::state(const std::string& target) const noexcept {
    const auto it = states_.find(normalize_target(target));
    return it == states_.end() ? nullptr : &it->second;
}

CommandExecutionSnapshot& GroundCommandExecutionEngine::state_for(const std::string& target) {
    const auto normalized = normalize_target(target);
    auto [it, inserted] = states_.try_emplace(normalized);
    if (inserted) {
        it->second.target = normalized;
    }
    return it->second;
}

std::string GroundCommandExecutionEngine::normalize_target(const std::string& target) {
    return uppercase_copy(target);
}

std::string GroundCommandExecutionEngine::status_name(CommandExecutionStatus status) {
    switch (status) {
    case CommandExecutionStatus::Executed: return "EXECUTED";
    case CommandExecutionStatus::InvalidCommand: return "INVALID_COMMAND";
    case CommandExecutionStatus::TargetUnavailable: return "TARGET_UNAVAILABLE";
    case CommandExecutionStatus::PreconditionsFailed: return "PRECONDITIONS_FAILED";
    case CommandExecutionStatus::UnsupportedOpcode: return "UNSUPPORTED_OPCODE";
    case CommandExecutionStatus::AlreadyInRequestedState: return "ALREADY_IN_REQUESTED_STATE";
    }
    return "UNKNOWN";
}

bool GroundCommandExecutionEngine::supported_target(const std::string& target) noexcept {
    return target == "VIKRAM" || target == "ROVER" || target == "LANDER";
}

GroundPacket GroundCommandExecutionEngine::build_event_packet(const GroundCommandRecord& command,
                                                               const CommandExecutionSnapshot& state,
                                                               const std::string& message,
                                                               std::uint32_t sequence,
                                                               CommandExecutionStatus status) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Event;
    packet.header.priority = GroundPacketPriority::High;
    packet.header.source_node = command.target;
    packet.header.origin_node = command.target;
    packet.header.destination_node = "GROUND";
    packet.header.application_id = command.application_id;
    packet.header.mission_timestamp_ns = command.mission_timestamp_ns;
    packet.header.sequence_number = sequence;
    const auto payload = make_payload({
        {"event", "COMMAND_EXECUTION"},
        {"command_id", command.command_id},
        {"opcode", command.opcode},
        {"target", state.target},
        {"status", status_name(status)},
        {"mode", state.mode},
        {"message", message}
    });
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

GroundPacket GroundCommandExecutionEngine::build_telemetry_packet(const GroundCommandRecord& command,
                                                                   const CommandExecutionSnapshot& state,
                                                                   std::uint32_t sequence) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Telemetry;
    packet.header.priority = GroundPacketPriority::Normal;
    packet.header.source_node = state.target;
    packet.header.origin_node = state.target;
    packet.header.destination_node = "GROUND";
    packet.header.application_id = command.application_id;
    packet.header.mission_timestamp_ns = command.mission_timestamp_ns;
    packet.header.sequence_number = sequence;
    const auto payload = make_payload({
        {"metric", "command_state"},
        {"target", state.target},
        {"mode", state.mode},
        {"safe_mode", state.safe_mode ? "true" : "false"},
        {"science_active", state.science_active ? "true" : "false"},
        {"transmit_requested", state.transmit_requested ? "true" : "false"},
        {"subsystem_reset_count", std::to_string(state.subsystem_reset_count)},
        {"commands_executed", std::to_string(state.commands_executed)}
    });
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

GroundPacket GroundCommandExecutionEngine::build_acknowledgement_packet(const GroundCommandRecord& command,
                                                                                const CommandExecutionResult& result,
                                                                                const std::uint32_t sequence) {
    GroundPacket packet{};
    packet.header.type = GroundPacketType::Event;
    packet.header.priority = result.status == CommandExecutionStatus::Executed
        ? GroundPacketPriority::High : GroundPacketPriority::Critical;
    packet.header.source_node = command.target;
    packet.header.origin_node = command.target;
    packet.header.destination_node = command.source_node.empty() ? "GROUND" : command.source_node;
    packet.header.application_id = command.application_id;
    packet.header.mission_timestamp_ns = command.mission_timestamp_ns;
    packet.header.sequence_number = sequence;
    const auto acknowledgement_status = result.status == CommandExecutionStatus::Executed
        ? "EXECUTED" : "REJECTED";
    const auto payload = make_payload({
        {"event", "COMMAND_ACKNOWLEDGEMENT"},
        {"command_id", command.command_id},
        {"target", command.target},
        {"opcode", command.opcode},
        {"status", acknowledgement_status},
        {"message", result.message}
    });
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

CommandExecutionResult GroundCommandExecutionEngine::execute(const GroundCommandRecord& command) {
    CommandExecutionResult result{};
    const auto target = normalize_target(command.target);
    if (!supported_target(target)) {
        result.status = CommandExecutionStatus::TargetUnavailable;
        result.message = "target is not executable: " + target;
        result.acknowledgement = {CommandAcknowledgementStatus::Rejected, command.command_id, target, command.opcode, result.message, 0U};
        result.acknowledgement_packet = build_acknowledgement_packet(command, result, next_acknowledgement_sequence_++);
        return result;
    }

    auto& state = state_for(target);
    const auto opcode = uppercase_copy(command.opcode);

    if (state.safe_mode && opcode != "ENTER_SAFE_MODE" && opcode != "EXIT_SAFE_MODE" && opcode != "RESET_SUBSYSTEM" && opcode != "SET_MODE") {
        result.status = CommandExecutionStatus::PreconditionsFailed;
        result.message = "target is in safe mode";
        result.state = state;
        result.event_packet = build_event_packet(command, state, result.message, next_event_sequence_++, result.status);
        result.acknowledgement = {CommandAcknowledgementStatus::Rejected, command.command_id, target, opcode, result.message, 0U};
        result.acknowledgement_packet = build_acknowledgement_packet(command, result, next_acknowledgement_sequence_++);
        result.telemetry_packet = build_telemetry_packet(command, state, next_telemetry_sequence_++);
        return result;
    }

    result.status = CommandExecutionStatus::Executed;
    result.message = "command executed";

    if (opcode == "SET_MODE") {
        const auto separator = command.parameters.find('=');
        const auto mode = separator == std::string::npos ? command.parameters : command.parameters.substr(separator + 1U);
        const auto normalized_mode = uppercase_copy(mode);
        if (normalized_mode.empty()) {
            result.status = CommandExecutionStatus::InvalidCommand;
            result.message = "SET_MODE requires a mode parameter";
        } else if (state.mode == normalized_mode) {
            result.status = CommandExecutionStatus::AlreadyInRequestedState;
            result.message = "target is already in requested mode";
        } else {
            state.mode = normalized_mode;
        }
    } else if (opcode == "START_ROVER") {
        if (target != "ROVER") {
            result.status = CommandExecutionStatus::PreconditionsFailed;
            result.message = "START_ROVER requires target ROVER";
        } else if (state.safe_mode) {
            result.status = CommandExecutionStatus::PreconditionsFailed;
            result.message = "rover is in safe mode";
        } else {
            state.mode = "DRIVING";
        }
    } else if (opcode == "STOP_ROVER" || opcode == "STOP") {
        state.mode = "SURFACE_READY";
        state.transmit_requested = false;
    } else if (opcode == "DRIVE") {
        if (target != "ROVER") {
            result.status = CommandExecutionStatus::PreconditionsFailed;
            result.message = "DRIVE requires target ROVER";
        } else {
            state.mode = "DRIVING";
        }
    } else if (opcode == "START_SCIENCE") {
        state.science_active = true;
        state.mode = "SCIENCE";
    } else if (opcode == "STOP_SCIENCE") {
        state.science_active = false;
        if (state.mode == "SCIENCE") state.mode = "SURFACE_READY";
    } else if (opcode == "TRANSMIT_DATA") {
        state.transmit_requested = true;
    } else if (opcode == "RESET_SUBSYSTEM") {
        ++state.subsystem_reset_count;
        state.transmit_requested = false;
        if (state.mode == "FAULT") state.mode = "STANDBY";
    } else if (opcode == "EXIT_SAFE_MODE") {
        state.safe_mode = false;
        if (state.mode == "SAFE") state.mode = "STANDBY";
    } else if (opcode == "ENTER_SAFE_MODE") {
        state.safe_mode = true;
        state.science_active = false;
        state.transmit_requested = false;
        state.mode = "SAFE";
    } else {
        result.status = CommandExecutionStatus::UnsupportedOpcode;
        result.message = "unsupported opcode: " + opcode;
    }

    if (result.status == CommandExecutionStatus::Executed) {
        ++state.commands_executed;
        state.last_command_id = command.command_id;
        state.last_opcode = opcode;
    }

    result.state = state;
    result.event_packet = build_event_packet(command, state, result.message, next_event_sequence_++, result.status);
    result.acknowledgement = {result.status == CommandExecutionStatus::Executed ? CommandAcknowledgementStatus::Executed : CommandAcknowledgementStatus::Rejected, command.command_id, target, opcode, result.message, 0U};
    result.acknowledgement_packet = build_acknowledgement_packet(command, result, next_acknowledgement_sequence_++);
    result.telemetry_packet = build_telemetry_packet(command, state, next_telemetry_sequence_++);
    if (result.status != CommandExecutionStatus::Executed) {
        result.event_packet.header.priority = GroundPacketPriority::Critical;
    }
    return result;
}

void GroundCommandExecutionEngine::reset() noexcept {
    states_.clear();
    next_event_sequence_ = static_cast<std::uint32_t>(configuration_.event_sequence_start);
    next_telemetry_sequence_ = static_cast<std::uint32_t>(configuration_.telemetry_sequence_start);
    next_acknowledgement_sequence_ = static_cast<std::uint32_t>(configuration_.acknowledgement_sequence_start);
}

} // namespace trishula
