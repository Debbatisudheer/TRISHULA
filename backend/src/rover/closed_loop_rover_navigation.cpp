#include "trishula/rover/closed_loop_rover_navigation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace trishula {

RoverClosedLoopNavigator::RoverClosedLoopNavigator(AutonomousRoverNavigator planner)
    : planner_(std::move(planner)) {}

double RoverClosedLoopNavigator::distance_to_polyline(const std::vector<RoverPathNode>& path,
                                                      std::size_t start_index,
                                                      double x_m,
                                                      double y_m) {
    if (path.empty()) {
        return std::numeric_limits<double>::infinity();
    }

    const std::size_t first = std::min(start_index, path.size() - 1U);
    double best = std::numeric_limits<double>::infinity();

    for (std::size_t i = first; i + 1U < path.size(); ++i) {
        const double ax = path[i].x_m;
        const double ay = path[i].y_m;
        const double bx = path[i + 1U].x_m;
        const double by = path[i + 1U].y_m;
        const double dx = bx - ax;
        const double dy = by - ay;
        const double length_sq = dx * dx + dy * dy;

        double t = 0.0;
        if (length_sq > 0.0) {
            t = ((x_m - ax) * dx + (y_m - ay) * dy) / length_sq;
            t = std::clamp(t, 0.0, 1.0);
        }

        const double px = ax + t * dx;
        const double py = ay + t * dy;
        best = std::min(best, std::hypot(x_m - px, y_m - py));
    }

    const auto& last = path.back();
    best = std::min(best, std::hypot(x_m - last.x_m, y_m - last.y_m));
    return best;
}


namespace {

double point_segment_distance(double px, double py,
                              double ax, double ay,
                              double bx, double by) {
    const double dx = bx - ax;
    const double dy = by - ay;
    const double length_sq = dx * dx + dy * dy;
    double t = 0.0;
    if (length_sq > 0.0) {
        t = ((px - ax) * dx + (py - ay) * dy) / length_sq;
        t = std::clamp(t, 0.0, 1.0);
    }
    const double cx = ax + t * dx;
    const double cy = ay + t * dy;
    return std::hypot(px - cx, py - cy);
}

bool path_intersects_obstacle(const std::vector<RoverPathNode>& path,
                              const RoverDynamicObstacle& obstacle,
                              std::size_t start_index) {
    if (!obstacle.active || path.empty()) return false;
    const std::size_t first = std::min(start_index, path.size() - 1U);
    for (std::size_t i = first; i + 1U < path.size(); ++i) {
        if (point_segment_distance(obstacle.x_m, obstacle.y_m,
                                   path[i].x_m, path[i].y_m,
                                   path[i + 1U].x_m, path[i + 1U].y_m) <= obstacle.radius_m) {
            return true;
        }
    }
    return std::hypot(path.back().x_m - obstacle.x_m,
                      path.back().y_m - obstacle.y_m) <= obstacle.radius_m;
}

} // namespace

RoverClosedLoopResult RoverClosedLoopNavigator::execute(const LunarTerrainMap& terrain,
                                                       SurfaceRover& rover,
                                                       RoverState& state,
                                                       const RoverMissionTarget& goal,
                                                       double half_extent_m,
                                                       std::size_t max_control_steps,
                                                       double deviation_threshold_m,
                                                       std::size_t disturbance_step,
                                                       double disturbance_x_m,
                                                       double disturbance_y_m,
                                                       double dt_s,
                                                       const RoverDynamicObstacle* dynamic_obstacle,
                                                       std::size_t obstacle_activation_step) const {
    if (half_extent_m <= 0.0 || max_control_steps == 0U || deviation_threshold_m <= 0.0 || dt_s <= 0.0) {
        throw std::invalid_argument("Invalid closed-loop rover navigation parameters");
    }
    if (!state.deployed || !state.mast_ready || !state.localization_valid) {
        throw std::invalid_argument("Rover must be deployed and surface-ready before autonomous navigation");
    }

    RoverClosedLoopResult result{};
    RoverMissionTarget start{state.x_m, state.y_m, goal.acceptance_radius_m};
    const RoverDynamicObstacle* active_obstacle = nullptr;
    auto route = planner_.plan(terrain, start, goal, half_extent_m, nullptr);
    result.path_found = route.path_found;
    if (!route.path_found) {
        return result;
    }

    std::size_t waypoint_index = route.path.size() > 1U ? 1U : 0U;
    result.path_replans = 0U;

    for (std::size_t step = 0; step < max_control_steps; ++step) {
        const bool obstacle_activated_now = dynamic_obstacle != nullptr &&
                                             obstacle_activation_step != 0U &&
                                             step == obstacle_activation_step;
        if (dynamic_obstacle != nullptr && obstacle_activation_step != 0U && step >= obstacle_activation_step) {
            active_obstacle = dynamic_obstacle;
        } else {
            active_obstacle = nullptr;
        }
        if (active_obstacle != nullptr && active_obstacle->active) {
            result.dynamic_obstacle_detected = true;
            const double dist = std::hypot(state.x_m - active_obstacle->x_m, state.y_m - active_obstacle->y_m);
            if (dist <= active_obstacle->radius_m) {
                state.speed_m_s = 0.0;
                state.mode = RoverMode::HazardHold;
                result.hazard_hold = true;
                result.control_steps = step;
                break;
            }
            if (obstacle_activated_now && path_intersects_obstacle(route.path, *active_obstacle,
                                                                     waypoint_index > 0U ? waypoint_index - 1U : 0U)) {
                RoverMissionTarget replan_start{state.x_m, state.y_m, goal.acceptance_radius_m};
                route = planner_.plan(terrain, replan_start, goal, half_extent_m, active_obstacle);
                ++result.path_replans;
                result.replanning_triggered = true;
                result.online_replan_completed = route.path_found;
                result.path_found = route.path_found;
                waypoint_index = route.path.size() > 1U ? 1U : 0U;
                if (!route.path_found) {
                    state.speed_m_s = 0.0;
                    state.mode = RoverMode::HazardHold;
                    result.hazard_hold = true;
                    result.control_steps = step;
                    break;
                }
            }
        }
        if (disturbance_step != 0U && step == disturbance_step) {
            state.x_m += disturbance_x_m;
            state.y_m += disturbance_y_m;
            result.replanning_triggered = true;
        }

        const auto current_metrics = rover.evaluate(state, terrain, goal);
        result.localization_valid = current_metrics.localization_valid;
        result.max_cross_track_error_m = std::max(
            result.max_cross_track_error_m,
            distance_to_polyline(route.path, waypoint_index > 0U ? waypoint_index - 1U : 0U,
                                 current_metrics.estimated_x_m,
                                 current_metrics.estimated_y_m));

        if (current_metrics.target_reached) {
            state.speed_m_s = 0.0;
            state.mode = RoverMode::MissionComplete;
            result.target_reached = true;
            result.control_steps = step;
            break;
        }

        const double cross_track = distance_to_polyline(route.path,
                                                         waypoint_index > 0U ? waypoint_index - 1U : 0U,
                                                         current_metrics.estimated_x_m,
                                                         current_metrics.estimated_y_m);
        if (cross_track > deviation_threshold_m) {
            RoverMissionTarget replan_start{current_metrics.estimated_x_m,
                                            current_metrics.estimated_y_m,
                                            goal.acceptance_radius_m};
            route = planner_.plan(terrain, replan_start, goal, half_extent_m, active_obstacle);
            if (active_obstacle != nullptr) {
                result.online_replan_completed = route.path_found;
            }
            ++result.path_replans;
            result.replanning_triggered = true;
            result.path_found = route.path_found;
            waypoint_index = route.path.size() > 1U ? 1U : 0U;
            if (!route.path_found) {
                state.speed_m_s = 0.0;
                state.mode = RoverMode::HazardHold;
                result.hazard_hold = true;
                result.control_steps = step;
                break;
            }
        }

        if (current_metrics.hazard_detected) {
            state.speed_m_s = 0.0;
            state.mode = RoverMode::HazardHold;
            result.hazard_hold = true;
            result.control_steps = step;
            break;
        }

        if (route.path.empty()) {
            state.speed_m_s = 0.0;
            state.mode = RoverMode::Fault;
            result.control_steps = step;
            break;
        }

        if (active_obstacle != nullptr && active_obstacle->active && waypoint_index < route.path.size()) {
            const auto& wp = route.path[waypoint_index];
            const double wp_dist = std::hypot(wp.x_m - active_obstacle->x_m, wp.y_m - active_obstacle->y_m);
            if (wp_dist <= active_obstacle->radius_m) {
                RoverMissionTarget replan_start{current_metrics.estimated_x_m, current_metrics.estimated_y_m, goal.acceptance_radius_m};
                route = planner_.plan(terrain, replan_start, goal, half_extent_m, active_obstacle);
                ++result.path_replans;
                result.replanning_triggered = true;
                result.online_replan_completed = route.path_found;
                result.path_found = route.path_found;
                waypoint_index = route.path.size() > 1U ? 1U : 0U;
                if (!route.path_found) {
                    state.speed_m_s = 0.0;
                    state.mode = RoverMode::HazardHold;
                    result.hazard_hold = true;
                    result.control_steps = step;
                    break;
                }
            }
        }

        while (waypoint_index + 1U < route.path.size()) {
            const double distance = std::hypot(route.path[waypoint_index].x_m - state.x_m,
                                               route.path[waypoint_index].y_m - state.y_m);
            if (distance <= goal.acceptance_radius_m) {
                ++waypoint_index;
                ++result.waypoint_advance_count;
            } else {
                break;
            }
        }

        const RoverPathNode waypoint = route.path[std::min(waypoint_index, route.path.size() - 1U)];
        const RoverMissionTarget waypoint_target{waypoint.x_m, waypoint.y_m, goal.acceptance_radius_m};
        const auto step_metrics = rover.step_toward_waypoint(state, terrain, waypoint_target, dt_s);
        result.localization_valid = step_metrics.localization_valid;
        result.max_cross_track_error_m = std::max(
            result.max_cross_track_error_m,
            distance_to_polyline(route.path, waypoint_index > 0U ? waypoint_index - 1U : 0U,
                                 step_metrics.estimated_x_m,
                                 step_metrics.estimated_y_m));
        result.control_steps = step + 1U;

        if (state.mode == RoverMode::HazardHold || state.mode == RoverMode::Fault) {
            result.hazard_hold = state.mode == RoverMode::HazardHold;
            break;
        }

        const auto final_metrics = rover.evaluate(state, terrain, goal);
        if (final_metrics.target_reached) {
            state.speed_m_s = 0.0;
            state.mode = RoverMode::MissionComplete;
            result.target_reached = true;
            break;
        }
    }

    const auto final_metrics = rover.evaluate(state, terrain, goal);
    result.final_distance_m = final_metrics.distance_to_target_m;
    result.estimated_final_x_m = final_metrics.estimated_x_m;
    result.estimated_final_y_m = final_metrics.estimated_y_m;
    result.localization_valid = final_metrics.localization_valid;
    result.target_reached = result.target_reached || final_metrics.target_reached;
    return result;
}

} // namespace trishula
