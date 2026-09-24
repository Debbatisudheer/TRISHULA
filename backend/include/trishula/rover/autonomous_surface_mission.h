#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "trishula/landing/lunar_terrain.h"
#include "trishula/rover/closed_loop_rover_navigation.h"
#include "trishula/rover/ground_science_processing.h"
#include "trishula/rover/science_archive.h"
#include "trishula/rover/science_data_products.h"
#include "trishula/rover/science_driven_mission_planner.h"
#include "trishula/rover/science_instruments.h"
#include "trishula/rover/science_knowledge.h"
#include "trishula/rover/science_relay.h"

namespace trishula {

struct AutonomousSurfaceMissionConfig {
    double navigation_half_extent_m{800.0};
    double control_dt_s{0.1};
    std::size_t max_control_steps_per_target{12000U};
    double deviation_threshold_m{4.0};
    double reserve_energy_wh{180.0};
    double route_energy_wh_per_m{0.75};
    double science_energy_wh{18.0};
    double minimum_science_quality{0.75};
    std::size_t max_targets{0U};
};

struct AutonomousSurfaceMissionTargetResult {
    std::size_t target_id{0U};
    bool navigation_completed{false};
    bool observation_completed{false};
    bool product_stored{false};
    bool rover_to_vikram{false};
    bool vikram_to_ground{false};
    bool ground_processed{false};
    bool knowledge_ingested{false};
    std::size_t control_steps{0U};
    double route_distance_m{0.0};
    double science_quality{0.0};
    double energy_used_wh{0.0};
    std::string product_id{};
};

struct AutonomousSurfaceMissionResult {
    bool deployment_completed{false};
    bool initialization_completed{false};
    bool mission_completed{false};
    bool stopped_by_energy_reserve{false};
    bool stopped_by_navigation_failure{false};
    std::size_t targets_completed{0U};
    std::size_t observations_completed{0U};
    std::size_t science_products_stored{0U};
    std::size_t rover_to_vikram_transfers{0U};
    std::size_t vikram_to_ground_transfers{0U};
    std::size_t ground_analyses_completed{0U};
    std::size_t knowledge_records{0U};
    double total_route_distance_m{0.0};
    double total_science_energy_wh{0.0};
    double surface_elapsed_s{0.0};
    RoverState final_rover_state{};
    std::vector<AutonomousSurfaceMissionTargetResult> target_results{};
};

class AutonomousSurfaceMission {
public:
    AutonomousSurfaceMission(AutonomousSurfaceMissionConfig config,
                              std::filesystem::path archive_root);

    [[nodiscard]] AutonomousSurfaceMissionResult execute(
        const LunarTerrainMap& terrain,
        SurfaceRover& rover,
        RoverState& state,
        std::vector<RoverScienceTarget> targets,
        const RoverDynamicObstacle* dynamic_obstacle = nullptr,
        std::size_t obstacle_activation_step = 0U);

private:
    AutonomousSurfaceMissionConfig config_{};
    std::filesystem::path archive_root_{};
    AutonomousRoverNavigator navigator_{};
    RoverClosedLoopNavigator closed_loop_;
    RoverScienceOperations science_operations_;
    LunarMaterialModel material_model_{};
    RoverScienceInstrumentSuite instruments_;
    ScienceDataProductBuilder product_builder_{};
    MissionKnowledgeBase knowledge_{};
    RoverScienceDrivenMissionPlanner mission_planner_{};
};

} // namespace trishula
