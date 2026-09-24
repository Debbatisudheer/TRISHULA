#include "trishula/navigation/terrain_relative_navigation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace trishula {

TerrainRelativeSensor::TerrainRelativeSensor(const LunarTerrainMap& terrain,
                                             double altitude_bias_m,
                                             double noise_sigma_m,
                                             double invalid_fraction,
                                             unsigned int seed)
    : terrain_(terrain), altitude_bias_m_(altitude_bias_m), noise_sigma_m_(noise_sigma_m),
      invalid_fraction_(invalid_fraction), seed_(seed) {
    if (noise_sigma_m_ < 0.0 || invalid_fraction_ < 0.0 || invalid_fraction_ >= 1.0) {
        throw std::invalid_argument("Invalid terrain-relative sensor parameters");
    }
}

TerrainMeasurement TerrainRelativeSensor::measure(double vehicle_altitude_m, double x_m) const {
    std::mt19937 rng(seed_ ^ static_cast<unsigned int>(std::llround((x_m + 10000.0) * 37.0)));
    std::normal_distribution<double> noise(0.0, noise_sigma_m_);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);
    const double truth_elevation = terrain_.elevation(x_m);
    const double range = vehicle_altitude_m - truth_elevation;
    const bool valid = uniform(rng) >= invalid_fraction_;
    const double measurement_noise = noise(rng);
    return {x_m, range + altitude_bias_m_ + measurement_noise,
            vehicle_altitude_m - (range + altitude_bias_m_ + measurement_noise),
            measurement_noise, valid};
}

std::vector<TerrainMeasurement> TerrainRelativeSensor::scan(double vehicle_altitude_m,
                                                             double center_x_m,
                                                             double half_width_m,
                                                             double spacing_m) const {
    if (half_width_m <= 0.0 || spacing_m <= 0.0) {
        throw std::invalid_argument("Terrain scan parameters must be positive");
    }
    std::vector<TerrainMeasurement> measurements;
    const int count = static_cast<int>(std::floor((2.0 * half_width_m) / spacing_m));
    measurements.reserve(static_cast<std::size_t>(count + 1));
    for (int i = 0; i <= count; ++i) {
        measurements.push_back(measure(vehicle_altitude_m,
                                       center_x_m - half_width_m + static_cast<double>(i) * spacing_m));
    }
    return measurements;
}

std::vector<TerrainEstimate> TerrainRelativeNavigator::estimate(
    const std::vector<TerrainMeasurement>& measurements, double spacing_m) const {
    if (measurements.size() < 3 || spacing_m <= 0.0) {
        throw std::invalid_argument("Terrain-relative estimation requires at least 3 measurements");
    }

    std::vector<TerrainEstimate> estimates;
    for (std::size_t i = 1; i + 1 < measurements.size(); ++i) {
        const auto& left = measurements[i - 1];
        const auto& center = measurements[i];
        const auto& right = measurements[i + 1];
        const double valid_count = static_cast<double>(left.valid + center.valid + right.valid);
        if (valid_count < 2.0) continue;

        const double slope = std::abs((right.estimated_elevation_m - left.estimated_elevation_m) /
                                      (2.0 * spacing_m));
        const double roughness = std::abs(center.estimated_elevation_m -
                                          0.5 * (left.estimated_elevation_m + right.estimated_elevation_m));
        const bool crater = center.estimated_elevation_m < std::min(left.estimated_elevation_m,
                                                                       right.estimated_elevation_m) - 10.0;
        const bool boulder = std::abs(right.estimated_elevation_m - left.estimated_elevation_m) > 8.0 &&
                             roughness > 1.5;
        double score = 180.0 * slope + 2.0 * roughness;
        score += crater ? 120.0 : 0.0;
        score += boulder ? 160.0 : 0.0;
        const double confidence = std::clamp(valid_count / 3.0, 0.0, 1.0) /
                                  (1.0 + roughness * 0.1);
        const bool safe = slope <= 0.035 && roughness <= 2.5 && !crater && !boulder && confidence >= 0.65;
        estimates.push_back({center.x_m, center.estimated_elevation_m, slope, roughness,
                             crater, boulder, score, safe, confidence});
    }
    return estimates;
}

TerrainEstimate TerrainRelativeNavigator::select_safe_site(const std::vector<TerrainEstimate>& estimates) const {
    TerrainEstimate best{};
    best.hazard_score = std::numeric_limits<double>::infinity();
    bool found = false;
    for (const auto& candidate : estimates) {
        if (!candidate.safe) continue;
        if (!found || candidate.hazard_score < best.hazard_score) {
            best = candidate;
            found = true;
        }
    }
    if (!found) {
        throw std::runtime_error("No safe site found from terrain-relative measurements");
    }
    return best;
}

} // namespace trishula
