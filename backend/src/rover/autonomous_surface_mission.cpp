#include "trishula/rover/autonomous_surface_mission.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace trishula {

AutonomousSurfaceMission::AutonomousSurfaceMission(AutonomousSurfaceMissionConfig config,
                                                   std::filesystem::path archive_root)
    : config_(config),
      archive_root_(std::move(archive_root)),
      navigator_(2.0, 0.5),
      closed_loop_(navigator_),
      science_operations_(config_.minimum_science_quality, config_.science_energy_wh),
      instruments_(material_model_),
      mission_planner_({1.2, 1.0, 0.015, 1.5, 0.02}) {
    if (config_.navigation_half_extent_m <= 0.0 || config_.control_dt_s <= 0.0 ||
        config_.max_control_steps_per_target == 0U || config_.deviation_threshold_m <= 0.0 ||
        config_.reserve_energy_wh < 0.0 || config_.route_energy_wh_per_m <= 0.0 ||
        config_.science_energy_wh <= 0.0) {
        throw std::invalid_argument("Invalid autonomous surface mission configuration");
    }
}

AutonomousSurfaceMissionResult AutonomousSurfaceMission::execute(
    const LunarTerrainMap& terrain,
    SurfaceRover& rover,
    RoverState& state,
    std::vector<RoverScienceTarget> targets,
    const RoverDynamicObstacle* dynamic_obstacle,
    std::size_t obstacle_activation_step) {
    AutonomousSurfaceMissionResult result{};

    result.deployment_completed = rover.deploy_from_lander(state);
    result.initialization_completed = rover.initialize_surface_systems(state);
    if (!result.deployment_completed || !result.initialization_completed) {
        result.final_rover_state = state;
        return result;
    }

    std::error_code ec;
    std::filesystem::remove_all(archive_root_, ec);
    ScienceDataArchive rover_archive(archive_root_ / "rover");
    ScienceDataArchive vikram_archive(archive_root_ / "vikram");
    ScienceDataArchive ground_archive(archive_root_ / "ground");
    GroundScienceProcessor ground_processor(archive_root_ / "analysis");
    ScienceDataRelay relay(archive_root_ / "rover", archive_root_ / "vikram", archive_root_ / "ground");

    RoverScienceDrivenMissionState mission_state{};
    mission_state.current_position = {state.x_m, state.y_m, 1.5};
    mission_state.available_energy_wh = std::max(0.0, state.battery_soc * 1000.0);
    mission_state.reserve_energy_wh = config_.reserve_energy_wh;
    mission_state.remaining_targets = std::move(targets);

    const std::size_t target_limit = config_.max_targets == 0U
                                         ? mission_state.remaining_targets.size()
                                         : std::min(config_.max_targets, mission_state.remaining_targets.size());

    for (std::size_t cycle = 0; cycle < target_limit && !mission_state.remaining_targets.empty(); ++cycle) {
        const RoverDynamicMissionState dynamic_state{
            mission_state.current_position,
            mission_state.available_energy_wh,
            mission_state.reserve_energy_wh,
            mission_state.remaining_targets,
            mission_state.obstacle};
        const auto plan = RoverDynamicMultiObjectiveMissionPlanner().replan(
            terrain, dynamic_state, navigator_, config_.navigation_half_extent_m,
            mission_state.previous_target_id, 0U);

        if (!plan.feasible) {
            result.stopped_by_energy_reserve = mission_state.available_energy_wh <= mission_state.reserve_energy_wh;
            result.stopped_by_navigation_failure = !result.stopped_by_energy_reserve;
            break;
        }

        const auto target = plan.assessment.target;
        AutonomousSurfaceMissionTargetResult target_result{};
        target_result.target_id = target.id;
        target_result.route_distance_m = plan.assessment.route.path_length_m;

        const double route_energy = plan.assessment.energy_cost_wh;
        if (route_energy + config_.science_energy_wh >
            std::max(0.0, mission_state.available_energy_wh - mission_state.reserve_energy_wh)) {
            result.stopped_by_energy_reserve = true;
            break;
        }

        const auto navigation = closed_loop_.execute(
            terrain, rover, state,
            RoverMissionTarget{target.x_m, target.y_m, 1.5},
            config_.navigation_half_extent_m,
            config_.max_control_steps_per_target,
            config_.deviation_threshold_m,
            0U, 0.0, 0.0, config_.control_dt_s,
            dynamic_obstacle, obstacle_activation_step);

        target_result.navigation_completed = navigation.target_reached && !navigation.hazard_hold;
        target_result.control_steps = navigation.control_steps;
        if (target_result.navigation_completed) {
            // MissionComplete is a terminal mode for a single navigation leg.
            // The surface mission controller re-arms the rover for the next autonomous leg.
            state.mode = RoverMode::SurfaceReady;
        }
        if (!target_result.navigation_completed) {
            result.stopped_by_navigation_failure = true;
            result.target_results.push_back(target_result);
            break;
        }

        const auto observation = instruments_.observe(target.id, state.x_m);
        const double observation_energy = config_.science_energy_wh;
        target_result.science_quality = observation.quality;
        target_result.observation_completed = observation.observation_valid;
        if (!target_result.observation_completed) {
            result.target_results.push_back(target_result);
            break;
        }

        auto product = product_builder_.create(
            observation,
            target.science_value * observation.quality,
            observation_energy,
            static_cast<std::uint64_t>(cycle + 1U),
            "SURFACE-SOL-" + std::to_string(cycle + 1U));
        if (!product_builder_.validate(product) || !product_builder_.mark_stored(product) ||
            !rover_archive.save(product)) {
            result.target_results.push_back(target_result);
            break;
        }
        target_result.product_stored = true;
        target_result.product_id = product.metadata.product_id;

        const auto relay_result = relay.transmit(product.metadata.product_id);
        target_result.rover_to_vikram = relay_result.rover_to_vikram;
        target_result.vikram_to_ground = relay_result.vikram_to_ground;
        if (!relay_result.rover_to_vikram || !relay_result.vikram_to_ground ||
            !relay_result.checksum_valid || !relay_result.ground_archive_persisted) {
            result.target_results.push_back(target_result);
            break;
        }

        ScienceDataProduct ground_product{};
        GroundScienceAnalysis analysis{};
        if (!ground_archive.load(product.metadata.product_id, ground_product) ||
            !ground_processor.process_product(ground_product, analysis) ||
            !analysis.scientifically_usable || !ground_processor.save(analysis)) {
            result.target_results.push_back(target_result);
            break;
        }
        target_result.ground_processed = true;
        target_result.knowledge_ingested = knowledge_.ingest(product);
        if (!target_result.knowledge_ingested) {
            result.target_results.push_back(target_result);
            break;
        }

        const double movement_energy = std::max(0.0, route_energy - target.required_energy_wh);
        const double energy_used = movement_energy + observation_energy + target.required_energy_wh;
        target_result.energy_used_wh = energy_used;
        mission_state.available_energy_wh = std::max(0.0, mission_state.available_energy_wh - energy_used);
        state.battery_soc = std::clamp(mission_state.available_energy_wh / 1000.0, 0.0, 1.0);
        mission_state.current_position = RoverMissionTarget{state.x_m, state.y_m, 1.5};
        mission_state.previous_target_id = target.id;

        mission_state.remaining_targets.erase(
            std::remove_if(mission_state.remaining_targets.begin(), mission_state.remaining_targets.end(),
                           [&](const RoverScienceTarget& candidate) { return candidate.id == target.id; }),
            mission_state.remaining_targets.end());

        result.targets_completed += 1U;
        result.observations_completed += 1U;
        result.science_products_stored += 1U;
        result.rover_to_vikram_transfers += relay_result.rover_to_vikram ? 1U : 0U;
        result.vikram_to_ground_transfers += relay_result.vikram_to_ground ? 1U : 0U;
        result.ground_analyses_completed += analysis.scientifically_usable ? 1U : 0U;
        result.total_route_distance_m += target_result.route_distance_m;
        result.total_science_energy_wh += observation_energy;
        result.surface_elapsed_s += static_cast<double>(navigation.control_steps) * config_.control_dt_s + 30.0;
        result.target_results.push_back(target_result);
    }

    result.knowledge_records = knowledge_.size();
    result.mission_completed = mission_state.remaining_targets.empty() && result.targets_completed > 0U;
    if (result.mission_completed) state.mode = RoverMode::MissionComplete;
    result.final_rover_state = state;
    return result;
}

} // namespace trishula
