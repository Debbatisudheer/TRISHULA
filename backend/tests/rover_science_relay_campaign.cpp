#include <filesystem>
#include <iostream>

#include "trishula/rover/science_archive.h"
#include "trishula/rover/science_data_products.h"
#include "trishula/rover/science_relay.h"

int main() {
    using namespace trishula;
    const std::filesystem::path root = std::filesystem::path("..") / "data" / "science_relay";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    const auto rover_root = root / "rover";
    const auto vikram_root = root / "vikram";
    const auto ground_root = root / "ground";

    RoverScienceObservation obs{};
    obs.target_id = 7U;
    obs.quality = 0.93;
    obs.energy_used_wh = 18.0;
    obs.acquired = true;
    obs.validated = true;
    obs.stored = true;

    ScienceDataProductBuilder builder;
    auto product = builder.create(obs, 17.67, 42U, "LUNAR-SOL-001234");
    if (!builder.validate(product) || !builder.mark_stored(product)) return 1;
    ScienceDataArchive rover(rover_root);
    if (!rover.save(product)) return 2;

    ScienceDataRelay relay(rover_root, vikram_root, ground_root);
    const auto first = relay.transmit(product.metadata.product_id);
    if (!first.rover_to_vikram || !first.vikram_to_ground || !first.checksum_valid ||
        !first.ground_archive_persisted || first.duplicate_rejected) return 3;

    ScienceDataArchive vikram(vikram_root);
    ScienceDataArchive ground(ground_root);
    ScienceDataProduct vikram_product{};
    ScienceDataProduct ground_product{};
    if (!vikram.load(product.metadata.product_id, vikram_product) ||
        !ground.load(product.metadata.product_id, ground_product)) return 4;
    if (vikram_product.metadata.product_id != product.metadata.product_id ||
        ground_product.metadata.product_id != product.metadata.product_id ||
        ground_product.metadata.provenance != product.metadata.provenance ||
        ground_product.science_score != product.science_score) return 5;

    const auto duplicate = relay.transmit(product.metadata.product_id);
    if (!duplicate.duplicate_rejected || duplicate.rover_to_vikram || duplicate.vikram_to_ground) return 6;

    const auto rover_count = rover.count();
    const auto vikram_count = vikram.count();
    const auto ground_count = ground.count();
    if (rover_count != 1U || vikram_count != 1U || ground_count != 1U) return 7;

    std::cout << "TRISHULA V0.9.45 - Rover -> Vikram -> Earth Science Relay\n"
              << "==============================================================\n"
              << "  rover science products       : " << rover_count << "\n"
              << "  rover -> Vikram transfer     : PASS\n"
              << "  checksum validation          : PASS\n"
              << "  Vikram science products      : " << vikram_count << "\n"
              << "  Vikram -> Ground transfer    : PASS\n"
              << "  ground archive products      : " << ground_count << "\n"
              << "  metadata preserved           : PASS\n"
              << "  duplicate packet rejection   : PASS\n"
              << "  end-to-end science relay     : PASS\n"
              << "  architecture                 : Rover -> Vikram -> Ground\n"
              << "\nV0.9.45 rover science relay campaign PASSED.\n";
    return 0;
}
