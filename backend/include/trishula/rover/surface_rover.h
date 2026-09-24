#pragma once

#include <cstddef>
#include <string>

#include "trishula/core/vector3.h"
#include "trishula/landing/lunar_terrain.h"

namespace trishula {

enum class RoverMode {
    Stowed,
    Deployment,
    SurfaceReady,
    Driving,
    HazardHold,
    MissionComplete,
    Fault
};

struct RoverParameters {
    double wheelbase_m{1.6};
    double track_m{1.4};
    double wheel_radius_m{0.25};
    double max_speed_m_s{0.5};
    double max_acceleration_m_s2{0.15};
    double max_slope{0.12};
    double max_roughness_m{0.30};
    double localization_noise_m{0.20};
    double obstacle_lookahead_m{3.0};
    double wheel_slip_limit{0.25};
};

struct RoverState {
    RoverMode mode{RoverMode::Stowed};
    double x_m{0.0};
    double y_m{0.0};
    double heading_rad{0.0};
    double speed_m_s{0.0};
    double battery_soc{1.0};
    bool deployed{false};
    bool mast_ready{false};
    bool drive_system_healthy{true};
    bool localization_valid{true};
    bool obstacle_detected{false};
    bool wheels_on_surface{false};
};

struct RoverMissionTarget {
    double x_m{0.0};
    double y_m{0.0};
    double acceptance_radius_m{1.5};
};

struct RoverStepMetrics {
    double distance_to_target_m{0.0};
    double estimated_x_m{0.0};
    double estimated_y_m{0.0};
    double terrain_slope{0.0};
    double terrain_roughness_m{0.0};
    bool hazard_detected{false};
    bool target_reached{false};
    bool localization_valid{false};
    bool wheel_slip_detected{false};
};

class SurfaceRover {
public:
    explicit SurfaceRover(RoverParameters parameters = {});

    [[nodiscard]] bool deploy_from_lander(RoverState& state) const;
    [[nodiscard]] bool initialize_surface_systems(RoverState& state) const;
    [[nodiscard]] RoverStepMetrics drive_to_target(RoverState& state,
                                                   const LunarTerrainMap& terrain,
                                                   const RoverMissionTarget& target,
                                                   std::size_t steps,
                                                   double dt_s = 0.1) const;
    [[nodiscard]] RoverStepMetrics step_toward_waypoint(RoverState& state,
                                                         const LunarTerrainMap& terrain,
                                                         const RoverMissionTarget& target,
                                                         double dt_s = 0.1) const;
    [[nodiscard]] RoverStepMetrics evaluate(const RoverState& state,
                                            const LunarTerrainMap& terrain,
                                            const RoverMissionTarget& target) const;
    [[nodiscard]] static const char* mode_name(RoverMode mode) noexcept;

private:
    RoverParameters parameters_{};
    [[nodiscard]] double terrain_slope_along_path(const LunarTerrainMap& terrain,
                                                  double x_m,
                                                  double y_m) const;
    [[nodiscard]] double terrain_roughness_along_path(const LunarTerrainMap& terrain,
                                                      double x_m,
                                                      double y_m) const;
    [[nodiscard]] bool hazard_ahead(const LunarTerrainMap& terrain,
                                    double x_m,
                                    double y_m,
                                    double heading_rad) const;
};

} // namespace trishula
