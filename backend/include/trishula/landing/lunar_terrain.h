#pragma once

#include <vector>

namespace trishula {

struct TerrainSample {
    double x_m{0.0};
    double elevation_m{0.0};
    double slope{0.0};
    double roughness_m{0.0};
    bool crater{false};
    bool boulder_field{false};
};

class LunarTerrainMap {
public:
    explicit LunarTerrainMap(double landing_zone_center_m = 0.0);

    [[nodiscard]] double elevation(double x_m) const;
    [[nodiscard]] double slope(double x_m) const;
    [[nodiscard]] double roughness(double x_m) const;
    [[nodiscard]] bool crater(double x_m) const;
    [[nodiscard]] bool boulder_field(double x_m) const;
    [[nodiscard]] TerrainSample sample(double x_m) const;

private:
    double center_m_;
};

struct LandingSiteCandidate {
    double x_m{0.0};
    double terrain_elevation_m{0.0};
    double slope{0.0};
    double roughness_m{0.0};
    bool crater{false};
    bool boulder_field{false};
    double hazard_score{0.0};
    bool safe{false};
};

class AutonomousLandingSiteSelector {
public:
    explicit AutonomousLandingSiteSelector(const LunarTerrainMap& terrain);

    [[nodiscard]] std::vector<LandingSiteCandidate> evaluate(double center_x_m,
                                                              double search_radius_m,
                                                              double spacing_m) const;
    [[nodiscard]] LandingSiteCandidate selectBest(double center_x_m,
                                                   double search_radius_m,
                                                   double spacing_m) const;

private:
    const LunarTerrainMap& terrain_;
};

} // namespace trishula
