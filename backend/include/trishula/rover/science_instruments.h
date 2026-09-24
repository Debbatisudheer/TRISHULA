#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace trishula {

struct LunarMaterialComposition {
    double si{0.0};
    double fe{0.0};
    double al{0.0};
    double ca{0.0};
    double mg{0.0};
    double ti{0.0};
};

struct LibsMeasurement {
    bool acquired{false};
    bool laser_fired{false};
    double laser_energy_mj{0.0};
    double plasma_temperature_k{0.0};
    double signal_to_noise{0.0};
    std::array<double, 6> line_intensity{};
    LunarMaterialComposition inferred_abundance{};
};

struct ApxsMeasurement {
    bool acquired{false};
    bool excitation_active{false};
    double exposure_s{0.0};
    double counts_per_second{0.0};
    std::array<double, 6> xray_counts{};
    LunarMaterialComposition inferred_abundance{};
};

struct SurfaceCameraMeasurement {
    bool acquired{false};
    double brightness{0.0};
    double texture_rms{0.0};
    double horizon_gradient{0.0};
    bool illuminated{false};
};

struct ScienceInstrumentObservation {
    std::size_t target_id{0U};
    double surface_x_m{0.0};
    LunarMaterialComposition truth_composition{};
    LibsMeasurement libs{};
    ApxsMeasurement apxs{};
    SurfaceCameraMeasurement camera{};
    bool instrument_suite_healthy{true};
    bool observation_valid{false};
    double quality{0.0};
};

class LunarMaterialModel {
public:
    [[nodiscard]] LunarMaterialComposition composition(double x_m) const;
};

class RoverScienceInstrumentSuite {
public:
    explicit RoverScienceInstrumentSuite(const LunarMaterialModel& material_model);

    [[nodiscard]] ScienceInstrumentObservation observe(std::size_t target_id,
                                                        double surface_x_m,
                                                        bool libs_healthy = true,
                                                        bool apxs_healthy = true,
                                                        bool camera_healthy = true) const;

private:
    const LunarMaterialModel& material_model_;
};

} // namespace trishula
