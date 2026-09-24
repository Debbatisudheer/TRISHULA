#include "trishula/rover/science_relay.h"

#include <functional>
#include <string>

namespace trishula {

ScienceDataRelay::ScienceDataRelay(std::filesystem::path rover_archive,
                                   std::filesystem::path vikram_archive,
                                   std::filesystem::path ground_archive)
    : rover_archive_(std::move(rover_archive)),
      vikram_archive_(std::move(vikram_archive)),
      ground_archive_(std::move(ground_archive)) {}

const std::filesystem::path& ScienceDataRelay::rover_archive() const noexcept { return rover_archive_; }
const std::filesystem::path& ScienceDataRelay::vikram_archive() const noexcept { return vikram_archive_; }
const std::filesystem::path& ScienceDataRelay::ground_archive() const noexcept { return ground_archive_; }

std::uint32_t ScienceDataRelay::checksum(const ScienceDataProduct& product) {
    std::string payload = product.metadata.product_id + "|" +
                          std::to_string(product.metadata.target_id) + "|" +
                          std::to_string(product.metadata.sequence) + "|" +
                          std::to_string(product.quality) + "|" +
                          std::to_string(product.science_score) + "|" +
                          std::to_string(product.energy_used_wh) + "|" +
                          std::to_string(product.measurement.surface_x_m) + "|" +
                          std::to_string(product.measurement.libs_laser_energy_mj) + "|" +
                          std::to_string(product.measurement.libs_plasma_temperature_k) + "|" +
                          std::to_string(product.measurement.libs_signal_to_noise) + "|" +
                          std::to_string(product.measurement.apxs_exposure_s) + "|" +
                          std::to_string(product.measurement.apxs_counts_per_second) + "|" +
                          std::to_string(product.measurement.camera_brightness) + "|" +
                          std::to_string(product.measurement.camera_texture_rms) + "|" +
                          std::to_string(product.measurement.camera_horizon_gradient);
    for (double value : product.measurement.libs_line_intensity) payload += "|" + std::to_string(value);
    for (double value : product.measurement.apxs_xray_counts) payload += "|" + std::to_string(value);
    for (double value : {product.measurement.libs_inferred_abundance.si, product.measurement.libs_inferred_abundance.fe,
                         product.measurement.libs_inferred_abundance.al, product.measurement.libs_inferred_abundance.ca,
                         product.measurement.libs_inferred_abundance.mg, product.measurement.libs_inferred_abundance.ti,
                         product.measurement.apxs_inferred_abundance.si, product.measurement.apxs_inferred_abundance.fe,
                         product.measurement.apxs_inferred_abundance.al, product.measurement.apxs_inferred_abundance.ca,
                         product.measurement.apxs_inferred_abundance.mg, product.measurement.apxs_inferred_abundance.ti}) {
        payload += "|" + std::to_string(value);
    }
    payload += product.measurement.camera_illuminated ? "|1" : "|0";
    return static_cast<std::uint32_t>(std::hash<std::string>{}(payload));
}

ScienceRelayResult ScienceDataRelay::transmit(const std::string& product_id) const {
    ScienceRelayResult result{};
    ScienceDataArchive rover(rover_archive_);
    ScienceDataArchive vikram(vikram_archive_);
    ScienceDataArchive ground(ground_archive_);

    ScienceDataProduct product{};
    if (!rover.load(product_id, product) || product.status != ScienceDataProductStatus::Stored) {
        return result;
    }

    const auto source_checksum = checksum(product);
    ScienceRelayPacket to_vikram{};
    to_vikram.packet_id = "RVR-" + product_id;
    to_vikram.product_id = product_id;
    to_vikram.source = RelayNode::Rover;
    to_vikram.destination = RelayNode::Vikram;
    to_vikram.sequence = product.metadata.sequence;
    to_vikram.checksum = source_checksum;

    ScienceDataProduct existing_vikram{};
    const bool duplicate = vikram.load(product_id, existing_vikram);
    if (duplicate) {
        result.duplicate_rejected = true;
        return result;
    }

    if (to_vikram.checksum != checksum(product)) return result;
    if (!vikram.save(product)) return result;
    result.rover_to_vikram = true;
    result.checksum_valid = true;

    ScienceDataProduct relayed{};
    if (!vikram.load(product_id, relayed)) return result;

    ScienceRelayPacket to_ground{};
    to_ground.packet_id = "VKM-" + product_id;
    to_ground.product_id = product_id;
    to_ground.source = RelayNode::Vikram;
    to_ground.destination = RelayNode::Ground;
    to_ground.sequence = relayed.metadata.sequence;
    to_ground.checksum = checksum(relayed);

    ScienceDataProduct existing_ground{};
    if (ground.load(product_id, existing_ground)) {
        result.duplicate_rejected = true;
        return result;
    }
    if (to_ground.checksum != checksum(relayed)) return result;
    if (!ground.save(relayed)) return result;
    result.vikram_to_ground = true;
    result.ground_archive_persisted = true;
    return result;
}

} // namespace trishula
