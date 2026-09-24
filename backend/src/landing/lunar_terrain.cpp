#include "trishula/landing/lunar_terrain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace trishula {
namespace {

double gaussian(double x, double center, double width) {
    const double z = (x - center) / width;
    return std::exp(-0.5 * z * z);
}

} // namespace

LunarTerrainMap::LunarTerrainMap(double landing_zone_center_m) : center_m_(landing_zone_center_m) {}

double LunarTerrainMap::elevation(double x_m) const {
    const double x = x_m - center_m_;
    const double broad_rise = 8.0 * std::sin(x / 900.0) + 3.5 * std::sin(x / 240.0 + 0.7);
    const double crater_1 = -22.0 * gaussian(x, -1650.0, 180.0);
    const double crater_2 = -14.0 * gaussian(x, 1250.0, 140.0);
    const double ridge = 9.0 * gaussian(x, 650.0, 210.0);
    return broad_rise + crater_1 + crater_2 + ridge;
}

double LunarTerrainMap::slope(double x_m) const {
    const double h = 1.0;
    return std::abs((elevation(x_m + h) - elevation(x_m - h)) / (2.0 * h));
}

double LunarTerrainMap::roughness(double x_m) const {
    const double h = 12.0;
    const double local = elevation(x_m);
    const double left = elevation(x_m - h);
    const double right = elevation(x_m + h);
    return std::abs(local - 0.5 * (left + right));
}

bool LunarTerrainMap::crater(double x_m) const {
    const double x = x_m - center_m_;
    return std::abs(x + 1650.0) < 300.0 || std::abs(x - 1250.0) < 240.0;
}

bool LunarTerrainMap::boulder_field(double x_m) const {
    const double x = x_m - center_m_;
    return std::abs(x - 650.0) < 260.0 || std::abs(std::fmod(x + 3000.0, 900.0)) < 35.0;
}

TerrainSample LunarTerrainMap::sample(double x_m) const {
    return {x_m, elevation(x_m), slope(x_m), roughness(x_m), crater(x_m), boulder_field(x_m)};
}

AutonomousLandingSiteSelector::AutonomousLandingSiteSelector(const LunarTerrainMap& terrain)
    : terrain_(terrain) {}

std::vector<LandingSiteCandidate> AutonomousLandingSiteSelector::evaluate(double center_x_m,
                                                                          double search_radius_m,
                                                                          double spacing_m) const {
    if (search_radius_m <= 0.0 || spacing_m <= 0.0) {
        throw std::invalid_argument("Landing-site search parameters must be positive");
    }

    std::vector<LandingSiteCandidate> candidates;
    const int count = static_cast<int>(std::floor((2.0 * search_radius_m) / spacing_m));
    candidates.reserve(static_cast<std::size_t>(count + 1));

    for (int i = 0; i <= count; ++i) {
        const double x = center_x_m - search_radius_m + static_cast<double>(i) * spacing_m;
        const auto sample = terrain_.sample(x);
        double score = 0.0;
        score += 180.0 * sample.slope;
        score += 2.0 * sample.roughness_m;
        score += sample.crater ? 120.0 : 0.0;
        score += sample.boulder_field ? 160.0 : 0.0;
        score += 0.002 * std::abs(x - center_x_m);
        const bool safe = sample.slope <= 0.035 && sample.roughness_m <= 2.5 && !sample.crater && !sample.boulder_field;
        candidates.push_back({x, sample.elevation_m, sample.slope, sample.roughness_m,
                              sample.crater, sample.boulder_field, score, safe});
    }
    return candidates;
}

LandingSiteCandidate AutonomousLandingSiteSelector::selectBest(double center_x_m,
                                                               double search_radius_m,
                                                               double spacing_m) const {
    const auto candidates = evaluate(center_x_m, search_radius_m, spacing_m);
    LandingSiteCandidate best{};
    best.hazard_score = std::numeric_limits<double>::infinity();
    bool found = false;
    for (const auto& candidate : candidates) {
        if (!candidate.safe) continue;
        if (!found || candidate.hazard_score < best.hazard_score) {
            best = candidate;
            found = true;
        }
    }
    if (!found) {
        throw std::runtime_error("No safe autonomous landing site found");
    }
    return best;
}

} // namespace trishula
