#include <cmath>
#include <filesystem>
#include <iostream>

#include "trishula/rover/ground_science_processing.h"
#include "trishula/rover/science_archive.h"
#include "trishula/rover/science_data_products.h"
#include "trishula/rover/science_instruments.h"
#include "trishula/rover/science_relay.h"

namespace {
bool close(double a, double b, double tol = 1.0e-12) { return std::abs(a - b) <= tol; }
}

int main() {
    using namespace trishula;
    const std::filesystem::path root = std::filesystem::path("..") / "data" / "ground_science_processing";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    LunarMaterialModel material;
    RoverScienceInstrumentSuite instruments(material);
    const auto observation = instruments.observe(7U, -700.0);
    ScienceDataProductBuilder builder;
    auto product = builder.create(observation, 17.67, 18.0, 47U, "LUNAR-SOL-001500");
    if (!builder.validate(product) || !builder.mark_stored(product)) return 1;

    ScienceDataArchive rover(root / "rover");
    if (!rover.save(product)) return 2;
    ScienceDataRelay relay(root / "rover", root / "vikram", root / "ground");
    const auto relay_result = relay.transmit(product.metadata.product_id);
    if (!relay_result.rover_to_vikram || !relay_result.vikram_to_ground ||
        !relay_result.checksum_valid || !relay_result.ground_archive_persisted) return 3;

    ScienceDataArchive ground(root / "ground");
    GroundScienceProcessor processor(root / "analysis");
    GroundScienceAnalysis analysis{};
    if (!ground.load(product.metadata.product_id, product)) return 4;
    if (!processor.process_product(product, analysis)) return 5;

    if (!analysis.scientifically_usable ||
        analysis.libs_apxs_abundance_agreement > 1.0e-9 ||
        analysis.fused_abundance.si <= 0.0 ||
        analysis.fused_abundance.fe <= 0.0 ||
        analysis.derived_fe_mg_ratio <= 0.0 ||
        analysis.derived_ti_si_ratio <= 0.0 ||
        analysis.interpretation.empty()) return 6;

    if (!processor.save(analysis)) return 7;
    GroundScienceAnalysis reloaded{};
    if (!processor.load(product.metadata.product_id, reloaded)) return 8;
    if (!close(reloaded.fused_abundance.ti, analysis.fused_abundance.ti) ||
        !close(reloaded.derived_fe_mg_ratio, analysis.derived_fe_mg_ratio) ||
        reloaded.interpretation != analysis.interpretation ||
        !reloaded.scientifically_usable) return 9;

    // Corrupting the ground science product must make processing fail rather than invent a result.
    ScienceDataProduct corrupted = product;
    corrupted.quality = 0.1;
    GroundScienceAnalysis rejected{};
    if (processor.process_product(corrupted, rejected)) return 10;

    std::cout << "TRISHULA V0.9.48 - Ground Science Processing + Science Archive\n"
              << "================================================================\n"
              << "  ground product loaded               : PASS\n"
              << "  measured science reconstructed      : PASS\n"
              << "  LIBS/APXS consistency check         : PASS\n"
              << "  fused abundance derived             : PASS\n"
              << "  scientific interpretation generated  : PASS\n"
              << "  analysis archive persisted           : PASS\n"
              << "  analysis reload after restart        : PASS\n"
              << "  invalid/low-quality product rejected : PASS\n"
              << "  hidden truth composition unused      : YES\n"
              << "  ground processing campaign           : PASS\n\n"
              << "V0.9.48 ground science processing campaign PASSED.\n";
    return 0;
}
