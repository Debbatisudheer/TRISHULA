#include "trishula/rover/science_instruments.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace trishula {
namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

LunarMaterialComposition normalize(LunarMaterialComposition c) {
    const double sum = c.si + c.fe + c.al + c.ca + c.mg + c.ti;
    if (sum <= 0.0) {
        throw std::runtime_error("Material composition must be positive");
    }
    c.si /= sum;
    c.fe /= sum;
    c.al /= sum;
    c.ca /= sum;
    c.mg /= sum;
    c.ti /= sum;
    return c;
}

std::array<double, 6> to_array(const LunarMaterialComposition& c) {
    return {c.si, c.fe, c.al, c.ca, c.mg, c.ti};
}

LunarMaterialComposition from_array(const std::array<double, 6>& a) {
    return {a[0], a[1], a[2], a[3], a[4], a[5]};
}

} // namespace

LunarMaterialComposition LunarMaterialModel::composition(double x_m) const {
    // Deterministic engineering composition field. It is not a lunar geochemical model.
    const double si = 0.35 + 0.07 * std::sin(x_m / 620.0);
    const double fe = 0.18 + 0.05 * std::cos(x_m / 410.0 + 0.3);
    const double al = 0.16 + 0.03 * std::sin(x_m / 830.0 + 0.8);
    const double ca = 0.12 + 0.025 * std::cos(x_m / 510.0 - 0.6);
    const double mg = 0.12 + 0.02 * std::sin(x_m / 290.0 + 1.4);
    const double ti = 0.07 + 0.018 * std::cos(x_m / 730.0 + 1.1);
    return normalize({si, fe, al, ca, mg, ti});
}

RoverScienceInstrumentSuite::RoverScienceInstrumentSuite(const LunarMaterialModel& material_model)
    : material_model_(material_model) {}

ScienceInstrumentObservation RoverScienceInstrumentSuite::observe(
    std::size_t target_id,
    double surface_x_m,
    bool libs_healthy,
    bool apxs_healthy,
    bool camera_healthy) const {
    ScienceInstrumentObservation result{};
    result.target_id = target_id;
    result.surface_x_m = surface_x_m;
    result.truth_composition = material_model_.composition(surface_x_m);
    result.instrument_suite_healthy = libs_healthy && apxs_healthy && camera_healthy;

    if (libs_healthy) {
        auto& m = result.libs;
        m.laser_fired = true;
        m.acquired = true;
        m.laser_energy_mj = 18.0;
        m.plasma_temperature_k = 9400.0 + 600.0 * result.truth_composition.fe;
        m.signal_to_noise = 42.0;

        const auto truth = to_array(result.truth_composition);
        for (std::size_t i = 0; i < truth.size(); ++i) {
            // Different line efficiencies model wavelength-dependent instrument response.
            const double efficiency = 0.80 + 0.04 * static_cast<double>(i);
            m.line_intensity[i] = truth[i] * efficiency * m.signal_to_noise;
        }
        std::array<double, 6> corrected{};
        for (std::size_t i = 0; i < corrected.size(); ++i) {
            const double efficiency = 0.80 + 0.04 * static_cast<double>(i);
            corrected[i] = m.line_intensity[i] / efficiency;
        }
        const double corrected_sum = std::accumulate(corrected.begin(), corrected.end(), 0.0);
        for (double& value : corrected) value /= corrected_sum;
        m.inferred_abundance = from_array(corrected);
    }

    if (apxs_healthy) {
        auto& m = result.apxs;
        m.excitation_active = true;
        m.acquired = true;
        m.exposure_s = 30.0;
        m.counts_per_second = 1850.0;

        const auto truth = to_array(result.truth_composition);
        for (std::size_t i = 0; i < truth.size(); ++i) {
            const double detector_efficiency = 0.55 + 0.06 * static_cast<double>(i);
            m.xray_counts[i] = truth[i] * m.counts_per_second * m.exposure_s * detector_efficiency;
        }
        std::array<double, 6> corrected{};
        for (std::size_t i = 0; i < corrected.size(); ++i) {
            const double detector_efficiency = 0.55 + 0.06 * static_cast<double>(i);
            corrected[i] = m.xray_counts[i] / detector_efficiency;
        }
        const double corrected_sum = std::accumulate(corrected.begin(), corrected.end(), 0.0);
        for (double& value : corrected) value /= corrected_sum;
        m.inferred_abundance = from_array(corrected);
    }

    if (camera_healthy) {
        auto& m = result.camera;
        m.acquired = true;
        const double x = surface_x_m;
        m.brightness = std::clamp(0.62 + 0.10 * std::sin(x / 500.0), 0.0, 1.0);
        m.texture_rms = 0.02 + 0.01 * std::abs(std::sin(x / 170.0));
        m.horizon_gradient = 0.03 * std::cos(x / 430.0);
        m.illuminated = true;
    }

    const bool all_acquired = result.libs.acquired && result.apxs.acquired && result.camera.acquired;
    if (all_acquired && result.instrument_suite_healthy) {
        result.observation_valid = true;
        result.quality = 0.94;
    } else if (result.libs.acquired || result.apxs.acquired || result.camera.acquired) {
        result.observation_valid = false;
        result.quality = 0.45;
    }

    return result;
}

} // namespace trishula
