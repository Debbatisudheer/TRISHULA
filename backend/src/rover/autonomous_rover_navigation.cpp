#include "trishula/rover/autonomous_rover_navigation.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <cstddef>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trishula {
namespace {

struct GridPoint {
    int ix{0};
    int iy{0};

    bool operator==(const GridPoint& other) const noexcept {
        return ix == other.ix && iy == other.iy;
    }
};

struct GridPointHash {
    std::size_t operator()(const GridPoint& point) const noexcept {
        const auto a = static_cast<std::uint64_t>(static_cast<std::int64_t>(point.ix));
        const auto b = static_cast<std::uint64_t>(static_cast<std::int64_t>(point.iy));
        return static_cast<std::size_t>((a * 0x9E3779B185EBCA87ULL) ^
                                        (b + 0xC2B2AE3D27D4EB4FULL + (a << 6U) + (a >> 2U)));
    }
};

struct QueueEntry {
    double f{0.0};
    GridPoint point{};

    bool operator>(const QueueEntry& other) const noexcept {
        return f > other.f;
    }
};

struct NodeData {
    double g{std::numeric_limits<double>::infinity()};
    GridPoint parent{};
    bool has_parent{false};
};

} // namespace

AutonomousRoverNavigator::AutonomousRoverNavigator(double grid_resolution_m,
                                                   double safety_margin_m)
    : grid_resolution_m_(grid_resolution_m), safety_margin_m_(safety_margin_m) {
    if (grid_resolution_m_ <= 0.0 || safety_margin_m_ < 0.0) {
        throw std::invalid_argument("Invalid rover navigation parameters");
    }
}

RoverNavigationResult AutonomousRoverNavigator::plan(const LunarTerrainMap& terrain,
                                                     const RoverMissionTarget& start,
                                                     const RoverMissionTarget& goal,
                                                     double half_extent_m,
                                                     const RoverDynamicObstacle* dynamic_obstacle) const {
    if (half_extent_m <= 0.0) {
        throw std::invalid_argument("Navigation search extent must be positive");
    }

    const int min_i = static_cast<int>(std::floor(-half_extent_m / grid_resolution_m_));
    const int max_i = static_cast<int>(std::ceil(half_extent_m / grid_resolution_m_));
    const int min_j = min_i;
    const int max_j = max_i;

    const auto to_grid = [&](double x, double y) {
        return GridPoint{static_cast<int>(std::llround(x / grid_resolution_m_)),
                         static_cast<int>(std::llround(y / grid_resolution_m_))};
    };
    const auto to_world = [&](GridPoint p) {
        return RoverPathNode{p.ix * grid_resolution_m_, p.iy * grid_resolution_m_};
    };
    const auto in_bounds = [&](GridPoint p) {
        return p.ix >= min_i && p.ix <= max_i && p.iy >= min_j && p.iy <= max_j;
    };
    const auto traversable = [&](GridPoint p) {
        if (!in_bounds(p)) return false;
        const auto world = to_world(p);
        if (dynamic_obstacle != nullptr && dynamic_obstacle->active &&
            std::hypot(world.x_m - dynamic_obstacle->x_m, world.y_m - dynamic_obstacle->y_m) <= dynamic_obstacle->radius_m) {
            return false;
        }
        const auto sample = terrain.sample(world.x_m);
        return !sample.crater && !sample.boulder_field &&
               sample.slope <= 0.12 && sample.roughness_m <= 0.30 + safety_margin_m_;
    };

    const GridPoint start_point = to_grid(start.x_m, start.y_m);
    const GridPoint goal_point = to_grid(goal.x_m, goal.y_m);

    RoverNavigationResult result{};
    result.target_safe = traversable(goal_point);
    if (!traversable(start_point) || !result.target_safe) {
        return result;
    }

    const auto heuristic = [&](GridPoint a, GridPoint b) {
        const double dx = static_cast<double>(a.ix - b.ix);
        const double dy = static_cast<double>(a.iy - b.iy);
        return std::sqrt(dx * dx + dy * dy) * grid_resolution_m_;
    };

    static constexpr int kDx[] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int kDy[] = {-1, -1, -1, 0, 0, 1, 1, 1};

    std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> open;
    std::unordered_map<GridPoint, NodeData, GridPointHash> nodes;
    std::unordered_map<GridPoint, bool, GridPointHash> closed;

    nodes[start_point].g = 0.0;
    open.push({heuristic(start_point, goal_point), start_point});

    GridPoint current{};
    bool found = false;
    while (!open.empty()) {
        current = open.top().point;
        open.pop();
        if (closed[current]) continue;
        closed[current] = true;
        ++result.expanded_nodes;

        if (current == goal_point) {
            found = true;
            break;
        }

        for (int k = 0; k < 8; ++k) {
            const GridPoint next{current.ix + kDx[k], current.iy + kDy[k]};
            if (!traversable(next) || closed[next]) continue;

            const double step = (kDx[k] == 0 || kDy[k] == 0)
                                    ? grid_resolution_m_
                                    : grid_resolution_m_ * std::numbers::sqrt2;
            const auto world = to_world(next);
            const auto sample = terrain.sample(world.x_m);
            const double terrain_penalty = 1.0 + 3.0 * sample.slope + 0.5 * sample.roughness_m;
            const double tentative_g = nodes[current].g + step * terrain_penalty;

            if (tentative_g < nodes[next].g) {
                nodes[next].g = tentative_g;
                nodes[next].parent = current;
                nodes[next].has_parent = true;
                open.push({tentative_g + heuristic(next, goal_point), next});
            }
        }
    }

    if (!found) return result;

    std::vector<RoverPathNode> reversed;
    GridPoint cursor = goal_point;
    reversed.push_back(to_world(cursor));
    while (!(cursor == start_point)) {
        const auto it = nodes.find(cursor);
        if (it == nodes.end() || !it->second.has_parent) {
            result.path.clear();
            result.path_found = false;
            return result;
        }
        cursor = it->second.parent;
        reversed.push_back(to_world(cursor));
    }
    std::reverse(reversed.begin(), reversed.end());
    result.path = std::move(reversed);
    result.path_found = true;
    result.estimated_cost = nodes[goal_point].g;

    for (std::size_t i = 1; i < result.path.size(); ++i) {
        const double dx = result.path[i].x_m - result.path[i - 1].x_m;
        const double dy = result.path[i].y_m - result.path[i - 1].y_m;
        result.path_length_m += std::hypot(dx, dy);
    }

    return result;
}

} // namespace trishula
