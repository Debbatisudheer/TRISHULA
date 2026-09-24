#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "trishula/ground/command_ingestion.h"

namespace trishula {

enum class CommandAcknowledgementStatus {
    Accepted,
    Executed,
    Rejected
};

enum class CommandExecutionStatus {
    Executed,
    InvalidCommand,
    TargetUnavailable,
    PreconditionsFailed,
    UnsupportedOpcode,
    AlreadyInRequestedState
};

struct CommandExecutionSnapshot {
    std::string target{};
    std::string mode{"STANDBY"};
    bool safe_mode{false};
    bool science_active{false};
    bool transmit_requested{false};
    std::uint32_t subsystem_reset_count{0U};
    std::uint64_t commands_executed{0U};
    std::string last_command_id{};
    std::string last_opcode{};
};

struct CommandExecutionAcknowledgement {
    CommandAcknowledgementStatus status{CommandAcknowledgementStatus::Rejected};
    std::string command_id{};
    std::string target{};
    std::string opcode{};
    std::string message{};
    std::uint32_t sequence_number{0U};
};

struct CommandExecutionResult {
    CommandExecutionStatus status{CommandExecutionStatus::InvalidCommand};
    std::string message{};
    CommandExecutionSnapshot state{};
    GroundPacket event_packet{};
    GroundPacket telemetry_packet{};
    GroundPacket acknowledgement_packet{};
    CommandExecutionAcknowledgement acknowledgement{};
};

struct CommandExecutionConfiguration {
    std::size_t event_sequence_start{100000U};
    std::size_t telemetry_sequence_start{200000U};
    std::size_t acknowledgement_sequence_start{250000U};
};

class GroundCommandExecutionEngine {
public:
    explicit GroundCommandExecutionEngine(CommandExecutionConfiguration configuration = {});

    [[nodiscard]] const CommandExecutionConfiguration& configuration() const noexcept;
    [[nodiscard]] const CommandExecutionSnapshot* state(const std::string& target) const noexcept;

    [[nodiscard]] CommandExecutionResult execute(const GroundCommandRecord& command);
    void reset() noexcept;

private:
    CommandExecutionSnapshot& state_for(const std::string& target);
    static std::string normalize_target(const std::string& target);
    static std::string status_name(CommandExecutionStatus status);
    static bool supported_target(const std::string& target) noexcept;
    static GroundPacket build_event_packet(const GroundCommandRecord& command,
                                            const CommandExecutionSnapshot& state,
                                            const std::string& message,
                                            std::uint32_t sequence,
                                            CommandExecutionStatus status);
    static GroundPacket build_telemetry_packet(const GroundCommandRecord& command,
                                                const CommandExecutionSnapshot& state,
                                                std::uint32_t sequence);
    static GroundPacket build_acknowledgement_packet(const GroundCommandRecord& command,
                                                      const CommandExecutionResult& result,
                                                      std::uint32_t sequence);

    CommandExecutionConfiguration configuration_{};
    std::unordered_map<std::string, CommandExecutionSnapshot> states_{};
    std::uint32_t next_event_sequence_{0U};
    std::uint32_t next_telemetry_sequence_{0U};
    std::uint32_t next_acknowledgement_sequence_{0U};
};

} // namespace trishula
