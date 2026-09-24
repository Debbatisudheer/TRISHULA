#include <cmath>
#include <iostream>

#include "trishula/rover/science_data_products.h"
#include "trishula/rover/science_knowledge.h"

int main() {
    using namespace trishula;

    RoverScienceObservation observation{};
    observation.target_id = 7U;
    observation.quality = 0.93;
    observation.energy_used_wh = 18.0;
    observation.acquired = true;
    observation.validated = true;
    observation.stored = true;

    ScienceDataProductBuilder builder;
    auto product = builder.create(observation, 17.67, 42U, "LUNAR-SOL-001234");
    const bool metadata_ok = product.metadata.product_id == "TRS-7-000042" &&
                             product.metadata.instrument == "surface_science_instrument" &&
                             !product.metadata.provenance.empty() &&
                             product.metadata.acquisition_time_tag == "LUNAR-SOL-001234";
    if (!metadata_ok || !builder.validate(product) || !builder.mark_stored(product)) return 1;

    MissionKnowledgeBase knowledge;
    if (!knowledge.ingest(product) || knowledge.size() != 1U) return 2;
    const auto* first = knowledge.find(7U);
    if (first == nullptr || first->observation_count != 1U || !first->high_quality ||
        std::abs(first->best_quality - 0.93) > 1e-12 ||
        first->latest_product_id != product.metadata.product_id) return 3;

    RoverScienceObservation followup = observation;
    followup.quality = 0.81;
    auto second = builder.create(followup, 8.1, 43U, "LUNAR-SOL-001260");
    if (!builder.validate(second) || !builder.mark_stored(second) || !knowledge.ingest(second)) return 4;
    const auto* aggregate = knowledge.find(7U);
    if (aggregate == nullptr || aggregate->observation_count != 2U ||
        std::abs(aggregate->cumulative_science_score - 25.77) > 1e-12 ||
        std::abs(aggregate->cumulative_energy_used_wh - 36.0) > 1e-12) return 5;

    RoverScienceObservation invalid{};
    invalid.target_id = 9U;
    invalid.quality = 1.1;
    invalid.acquired = true;
    auto rejected = builder.create(invalid, 2.0, 44U, "LUNAR-SOL-001300");
    if (builder.validate(rejected) || rejected.status != ScienceDataProductStatus::Rejected || knowledge.ingest(rejected)) return 6;

    const auto high_quality = knowledge.high_quality_records(0.85);
    if (high_quality.size() != 1U) return 7;

    std::cout << "TRISHULA V0.9.43 - Science Data Products + Mission Knowledge\n"
              << "==============================================================\n"
              << "  science data products created    : 2\n"
              << "  metadata/provenance               : PASS\n"
              << "  data quality validation            : PASS\n"
              << "  persistent storage state           : PASS\n"
              << "  mission knowledge records          : " << knowledge.size() << "\n"
              << "  aggregated observations            : " << aggregate->observation_count << "\n"
              << "  cumulative science score          : " << aggregate->cumulative_science_score << "\n"
              << "  cumulative energy tracked         : " << aggregate->cumulative_energy_used_wh << " Wh\n"
              << "  high-quality knowledge query       : PASS\n"
              << "  rejected invalid product           : PASS\n"
              << "  provenance-aware mission knowledge : PASS\n"
              << "\nV0.9.43 science data products + mission knowledge campaign PASSED.\n";
    return 0;
}
