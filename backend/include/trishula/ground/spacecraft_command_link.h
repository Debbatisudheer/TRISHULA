#pragma once

#include <cstdint>
#include <string>

#include "trishula/core/simulation_engine.h"
#include "trishula/ground/command_ingestion.h"

namespace trishula {

enum class SpacecraftCommandStatus {
    Applied,
    InvalidTarget,
    InvalidOpcode,
    InvalidParameters,
    PreconditionsFailed,
    PhysicalFault
};

struct SpacecraftCommandResult {
    SpacecraftCommandStatus status{SpacecraftCommandStatus::PreconditionsFailed};
    std::string message{};
    std::uint32_t sequence_number{0U};
    double propagated_seconds{0.0};
    TrueState state_before{};
    TrueState state_after{};
    PropulsionOutput propulsion{};
};

struct SpacecraftCommandLinkConfiguration {
    std::string spacecraft_target{"VIKRAM"};
    double maximum_propagation_seconds{10.0};
    double minimum_propagation_seconds{1.0e-6};
};

class GroundSpacecraftCommandLink {
public:
    explicit GroundSpacecraftCommandLink(SpacecraftCommandLinkConfiguration configuration = {});

    [[nodiscard]] const SpacecraftCommandLinkConfiguration& configuration() const noexcept;
    [[nodiscard]] std::uint32_t last_sequence() const noexcept;

    SpacecraftCommandResult apply(const GroundCommandRecord& command, SimulationEngine& simulation);
    void reset() noexcept;

private:
    static bool parse_double_parameter(const std::string& parameters,
                                       const std::string& key,
                                       double& value);
    static bool parse_vector_parameter(const std::string& parameters,
                                       const char* prefix,
                                       Vector3& value);
    static std::string normalize(const std::string& value);
    static std::string status_name(SpacecraftCommandStatus status);

    SpacecraftCommandLinkConfiguration configuration_{};
    std::uint32_t last_sequence_{0U};
};

} // namespace trishula
