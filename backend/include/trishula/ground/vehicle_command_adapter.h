#pragma once

#include <cstdint>
#include <string>

#include "trishula/ground/command_ingestion.h"
#include "trishula/ground/command_execution.h"
#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/surface_rover.h"

namespace trishula {

enum class VehicleAdapterStatus {
    Applied,
    UnsupportedTarget,
    UnsupportedOpcode,
    PreconditionsFailed,
    InvalidParameters,
    PhysicalFault
};

struct VehicleAdapterSnapshot {
    std::string target{"ROVER"};
    std::string mode{"STOWED"};
    double x_m{0.0};
    double y_m{0.0};
    double heading_rad{0.0};
    double speed_m_s{0.0};
    double battery_soc{1.0};
    bool deployed{false};
    bool mast_ready{false};
    bool localization_valid{true};
    bool hazard_detected{false};
    bool drive_system_healthy{true};
    std::uint64_t physical_steps{0U};
    std::string last_command_id{};
    std::string last_opcode{};
};

struct VehicleAdapterResult {
    VehicleAdapterStatus status{VehicleAdapterStatus::PreconditionsFailed};
    std::string message{};
    VehicleAdapterSnapshot snapshot{};
    RoverStepMetrics rover_metrics{};
};

struct VehicleCommandAdapterConfiguration {
    double default_drive_dt_s{0.1};
    RoverParameters rover_parameters{};
    double terrain_center_m{0.0};
};

class VehicleCommandAdapter {
public:
    explicit VehicleCommandAdapter(VehicleCommandAdapterConfiguration configuration = {});

    [[nodiscard]] const VehicleCommandAdapterConfiguration& configuration() const noexcept;
    [[nodiscard]] const VehicleAdapterSnapshot& snapshot() const noexcept;

    VehicleAdapterResult apply(const GroundCommandRecord& command,
                               const CommandExecutionResult& execution_result);

    void reset() noexcept;

private:
    static std::string normalize(const std::string& value);
    static bool parse_double_parameter(const std::string& parameters,
                                       const std::string& key,
                                       double& value);
    static bool has_parameter(const std::string& parameters, const std::string& key);

    VehicleAdapterResult apply_rover(const GroundCommandRecord& command);
    VehicleAdapterResult apply_unsupported_target(const GroundCommandRecord& command);
    void sync_snapshot(const std::string& command_id, const std::string& opcode);

    VehicleCommandAdapterConfiguration configuration_{};
    LunarTerrainMap terrain_;
    SurfaceRover rover_;
    RoverState rover_state_{};
    VehicleAdapterSnapshot snapshot_{};
};

} // namespace trishula
