#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "trishula/rover/science_instruments.h"
#include "trishula/rover/science_operations.h"

namespace trishula {

enum class ScienceDataProductStatus { Draft, Validated, Stored, Rejected };

struct ScienceDataProductMetadata {
    std::string product_id{};
    std::size_t target_id{0U};
    std::uint64_t sequence{0U};
    std::string instrument{"surface_science_instrument"};
    std::string acquisition_time_tag{};
    std::string provenance{"TRISHULA rover science acquisition"};
};

struct ScienceMeasurementData {
    double surface_x_m{0.0};
    double libs_laser_energy_mj{0.0};
    double libs_plasma_temperature_k{0.0};
    double libs_signal_to_noise{0.0};
    std::array<double, 6> libs_line_intensity{};
    LunarMaterialComposition libs_inferred_abundance{};
    double apxs_exposure_s{0.0};
    double apxs_counts_per_second{0.0};
    std::array<double, 6> apxs_xray_counts{};
    LunarMaterialComposition apxs_inferred_abundance{};
    double camera_brightness{0.0};
    double camera_texture_rms{0.0};
    double camera_horizon_gradient{0.0};
    bool camera_illuminated{false};
};

struct ScienceDataProduct {
    ScienceDataProductMetadata metadata{};
    double quality{0.0};
    double science_score{0.0};
    double energy_used_wh{0.0};
    ScienceDataProductStatus status{ScienceDataProductStatus::Draft};
    bool acquired{false};
    bool validated{false};
    bool stored{false};
    ScienceMeasurementData measurement{};
};

class ScienceDataProductBuilder {
public:
    ScienceDataProduct create(const RoverScienceObservation& observation,
                              double science_score,
                              std::uint64_t sequence,
                              std::string acquisition_time_tag = {}) const;

    ScienceDataProduct create(const ScienceInstrumentObservation& observation,
                              double science_score,
                              double energy_used_wh,
                              std::uint64_t sequence,
                              std::string acquisition_time_tag = {}) const;

    bool validate(ScienceDataProduct& product) const;
    bool mark_stored(ScienceDataProduct& product) const;
};

} // namespace trishula
