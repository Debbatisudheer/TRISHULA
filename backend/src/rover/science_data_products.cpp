#include "trishula/rover/science_data_products.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace trishula {

ScienceDataProduct ScienceDataProductBuilder::create(const RoverScienceObservation& observation,
                                                     double science_score,
                                                     std::uint64_t sequence,
                                                     std::string acquisition_time_tag) const {
    ScienceDataProduct product{};
    product.metadata.target_id = observation.target_id;
    product.metadata.sequence = sequence;
    product.metadata.acquisition_time_tag = std::move(acquisition_time_tag);
    product.quality = observation.quality;
    product.science_score = std::max(0.0, science_score);
    product.energy_used_wh = std::max(0.0, observation.energy_used_wh);
    product.acquired = observation.acquired;
    product.validated = observation.validated;
    product.stored = observation.stored;

    std::ostringstream id;
    id << "TRS-" << product.metadata.target_id << '-' << std::setw(6) << std::setfill('0') << sequence;
    product.metadata.product_id = id.str();
    return product;
}

ScienceDataProduct ScienceDataProductBuilder::create(const ScienceInstrumentObservation& observation,
                                                     double science_score,
                                                     double energy_used_wh,
                                                     std::uint64_t sequence,
                                                     std::string acquisition_time_tag) const {
    ScienceDataProduct product{};
    product.metadata.target_id = observation.target_id;
    product.metadata.sequence = sequence;
    product.metadata.acquisition_time_tag = std::move(acquisition_time_tag);
    product.quality = observation.quality;
    product.science_score = std::max(0.0, science_score);
    product.energy_used_wh = std::max(0.0, energy_used_wh);
    product.acquired = observation.libs.acquired || observation.apxs.acquired || observation.camera.acquired;

    product.measurement.surface_x_m = observation.surface_x_m;
    product.measurement.libs_laser_energy_mj = observation.libs.laser_energy_mj;
    product.measurement.libs_plasma_temperature_k = observation.libs.plasma_temperature_k;
    product.measurement.libs_signal_to_noise = observation.libs.signal_to_noise;
    product.measurement.libs_line_intensity = observation.libs.line_intensity;
    product.measurement.libs_inferred_abundance = observation.libs.inferred_abundance;
    product.measurement.apxs_exposure_s = observation.apxs.exposure_s;
    product.measurement.apxs_counts_per_second = observation.apxs.counts_per_second;
    product.measurement.apxs_xray_counts = observation.apxs.xray_counts;
    product.measurement.apxs_inferred_abundance = observation.apxs.inferred_abundance;
    product.measurement.camera_brightness = observation.camera.brightness;
    product.measurement.camera_texture_rms = observation.camera.texture_rms;
    product.measurement.camera_horizon_gradient = observation.camera.horizon_gradient;
    product.measurement.camera_illuminated = observation.camera.illuminated;

    std::ostringstream id;
    id << "TRS-" << product.metadata.target_id << '-' << std::setw(6) << std::setfill('0') << sequence;
    product.metadata.product_id = id.str();
    return product;
}

bool ScienceDataProductBuilder::validate(ScienceDataProduct& product) const {
    const bool valid = product.acquired && product.quality > 0.0 && product.quality <= 1.0 &&
                       product.science_score >= 0.0 && product.energy_used_wh >= 0.0;
    product.validated = valid;
    product.status = valid ? ScienceDataProductStatus::Validated : ScienceDataProductStatus::Rejected;
    return valid;
}

bool ScienceDataProductBuilder::mark_stored(ScienceDataProduct& product) const {
    if (!product.validated || product.metadata.product_id.empty()) {
        product.stored = false;
        return false;
    }
    product.stored = true;
    product.status = ScienceDataProductStatus::Stored;
    return true;
}

} // namespace trishula
