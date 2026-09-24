#pragma once

#include "trishula/landing/lunar_terrain.h"

#include <cstddef>
#include <vector>

namespace trishula {

struct TerrainMeasurement {
    double x_m{0.0};
    double terrain_relative_range_m{0.0};
    double estimated_elevation_m{0.0};
    double noise_m{0.0};
    bool valid{true};
};

class TerrainRelativeSensor {
public:
    TerrainRelativeSensor(const LunarTerrainMap& terrain,
                          double altitude_bias_m = 0.0,
                          double noise_sigma_m = 0.0,
                          double invalid_fraction = 0.0,
                          unsigned int seed = 0x31415926u);

    [[nodiscard]] TerrainMeasurement measure(double vehicle_altitude_m, double x_m) const;
    [[nodiscard]] std::vector<TerrainMeasurement> scan(double vehicle_altitude_m,
                                                        double center_x_m,
                                                        double half_width_m,
                                                        double spacing_m) const;

private:
    const LunarTerrainMap& terrain_;
    double altitude_bias_m_;
    double noise_sigma_m_;
    double invalid_fraction_;
    unsigned int seed_;
};

struct TerrainEstimate {
    double x_m{0.0};
    double elevation_m{0.0};
    double slope{0.0};
    double roughness_m{0.0};
    bool crater{false};
    bool boulder_field{false};
    double hazard_score{0.0};
    bool safe{false};
    double confidence{0.0};
};

class TerrainRelativeNavigator {
public:
    [[nodiscard]] std::vector<TerrainEstimate> estimate(const std::vector<TerrainMeasurement>& measurements,
                                                         double spacing_m) const;

    [[nodiscard]] TerrainEstimate select_safe_site(const std::vector<TerrainEstimate>& estimates) const;
};

} // namespace trishula
