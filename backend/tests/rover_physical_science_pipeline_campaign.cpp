#include <cmath>
#include <filesystem>
#include <iostream>

#include "trishula/rover/science_archive.h"
#include "trishula/rover/science_data_products.h"
#include "trishula/rover/science_instruments.h"
#include "trishula/rover/science_relay.h"

namespace {

bool close(double a, double b, double tol = 1.0e-12) { return std::abs(a - b) <= tol; }

}

int main() {
    using namespace trishula;
    const std::filesystem::path root = std::filesystem::path("..") / "data" / "science_pipeline";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    LunarMaterialModel material;
    RoverScienceInstrumentSuite instruments(material);
    const auto observation = instruments.observe(7U, -700.0);
    if (!observation.observation_valid || !observation.libs.acquired || !observation.apxs.acquired || !observation.camera.acquired) return 1;

    ScienceDataProductBuilder builder;
    auto product = builder.create(observation, 17.67, 18.0, 47U, "LUNAR-SOL-001500");
    if (!builder.validate(product) || !builder.mark_stored(product)) return 2;

    // The product must contain measured raw/calibrated instrument data, not hidden truth composition.
    if (!close(product.measurement.surface_x_m, -700.0) ||
        product.measurement.libs_signal_to_noise <= 0.0 ||
        product.measurement.libs_line_intensity[0] <= 0.0 ||
        product.measurement.apxs_xray_counts[0] <= 0.0 ||
        product.measurement.libs_inferred_abundance.si <= 0.0 ||
        product.measurement.apxs_inferred_abundance.fe <= 0.0 ||
        product.measurement.camera_illuminated != observation.camera.illuminated) return 3;

    ScienceDataArchive rover(root / "rover");
    if (!rover.save(product)) return 4;

    ScienceDataProduct rover_loaded{};
    if (!rover.load(product.metadata.product_id, rover_loaded)) return 5;
    if (!close(rover_loaded.measurement.libs_line_intensity[2], product.measurement.libs_line_intensity[2]) ||
        !close(rover_loaded.measurement.apxs_xray_counts[4], product.measurement.apxs_xray_counts[4]) ||
        !close(rover_loaded.measurement.libs_inferred_abundance.ti, product.measurement.libs_inferred_abundance.ti)) return 6;

    ScienceDataRelay relay(root / "rover", root / "vikram", root / "ground");
    const auto result = relay.transmit(product.metadata.product_id);
    if (!result.rover_to_vikram || !result.vikram_to_ground || !result.checksum_valid || !result.ground_archive_persisted) return 7;

    ScienceDataArchive vikram(root / "vikram");
    ScienceDataArchive ground(root / "ground");
    ScienceDataProduct ground_loaded{};
    if (!vikram.load(product.metadata.product_id, rover_loaded) ||
        !ground.load(product.metadata.product_id, ground_loaded)) return 8;

    if (!close(ground_loaded.measurement.surface_x_m, observation.surface_x_m) ||
        !close(ground_loaded.measurement.libs_plasma_temperature_k, observation.libs.plasma_temperature_k) ||
        !close(ground_loaded.measurement.libs_line_intensity[5], observation.libs.line_intensity[5]) ||
        !close(ground_loaded.measurement.apxs_xray_counts[1], observation.apxs.xray_counts[1]) ||
        !close(ground_loaded.measurement.camera_brightness, observation.camera.brightness) ||
        ground_loaded.metadata.provenance != product.metadata.provenance) return 9;

    std::cout << "TRISHULA V0.9.47 - Physical Science -> Data Product -> Persistent Storage -> Rover -> Vikram -> Ground\n"
              << "=======================================================================\n"
              << "  instrument observation            : PASS\n"
              << "  raw LIBS signal captured           : PASS\n"
              << "  raw APXS signal captured           : PASS\n"
              << "  calibrated science retained        : PASS\n"
              << "  science data product built         : PASS\n"
              << "  rover persistent archive           : PASS\n"
              << "  rover -> Vikram transfer            : PASS\n"
              << "  end-to-end checksum                : PASS\n"
              << "  Vikram persistent archive           : PASS\n"
              << "  Vikram -> Ground transfer           : PASS\n"
              << "  ground persistent archive           : PASS\n"
              << "  measurement fidelity after relay  : PASS\n"
              << "  truth composition omitted from product: YES\n"
              << "\nV0.9.47 physical science pipeline campaign PASSED.\n";
    return 0;
}
