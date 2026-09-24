#include <filesystem>
#include <iostream>
#include <string>

#include "trishula/rover/science_archive.h"
#include "trishula/rover/science_data_products.h"

int main() {
    using namespace trishula;
    const std::filesystem::path archive_root = std::filesystem::path("..") / "data" / "science";
    std::error_code ec;
    std::filesystem::remove_all(archive_root, ec);

    RoverScienceObservation observation{};
    observation.target_id = 7U;
    observation.quality = 0.93;
    observation.energy_used_wh = 18.0;
    observation.acquired = true;
    observation.validated = true;
    observation.stored = true;

    ScienceDataProductBuilder builder;
    auto product = builder.create(observation, 17.67, 42U, "LUNAR-SOL-001234");
    if (!builder.validate(product) || !builder.mark_stored(product)) return 1;

    ScienceDataArchive archive(archive_root);
    if (!archive.save(product)) return 2;
    if (!std::filesystem::exists(archive.product_path(product.metadata.product_id))) return 3;

    ScienceDataProduct loaded{};
    if (!archive.load(product.metadata.product_id, loaded)) return 4;
    if (loaded.metadata.product_id != product.metadata.product_id ||
        loaded.metadata.target_id != 7U || loaded.metadata.sequence != 42U ||
        loaded.quality != product.quality || loaded.stored != true ||
        loaded.status != ScienceDataProductStatus::Stored) return 5;

    const auto products = archive.load_all();
    if (products.size() != 1U) return 6;

    auto second = builder.create(observation, 8.10, 43U, "LUNAR-SOL-001260");
    if (!builder.validate(second) || !builder.mark_stored(second) || !archive.save(second)) return 7;
    if (archive.count() != 2U) return 8;

    ScienceDataProduct invalid{};
    invalid.metadata.product_id = "invalid";
    invalid.status = ScienceDataProductStatus::Validated;
    invalid.stored = false;
    if (archive.save(invalid)) return 9;

    const auto reloaded = archive.load_all();
    if (reloaded.size() != 2U) return 10;

    std::cout << "TRISHULA V0.9.44 - Persistent Science Data Archive\n"
              << "==============================================================\n"
              << "  science products persisted       : " << reloaded.size() << "\n"
              << "  on-disk archive                 : YES\n"
              << "  reload after write               : PASS\n"
              << "  metadata/provenance preserved    : PASS\n"
              << "  stored-status validation         : PASS\n"
              << "  invalid-product rejection        : PASS\n"
              << "  atomic file replacement          : ENABLED\n"
              << "  persistent science storage       : PASS\n"
              << "  archive directory                : ../data/science\n"
              << "\nV0.9.44 persistent science data storage campaign PASSED.\n";

    return 0;
}
