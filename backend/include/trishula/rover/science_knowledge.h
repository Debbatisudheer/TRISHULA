#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "trishula/rover/science_data_products.h"

namespace trishula {

struct MissionKnowledgeRecord {
    std::size_t target_id{0U};
    double best_quality{0.0};
    double cumulative_science_score{0.0};
    double cumulative_energy_used_wh{0.0};
    std::size_t observation_count{0U};
    bool high_quality{false};
    std::string latest_product_id{};
};

class MissionKnowledgeBase {
public:
    bool ingest(const ScienceDataProduct& product);
    [[nodiscard]] const MissionKnowledgeRecord* find(std::size_t target_id) const;
    [[nodiscard]] std::vector<MissionKnowledgeRecord> high_quality_records(double threshold) const;
    [[nodiscard]] std::size_t size() const noexcept;

private:
    std::vector<MissionKnowledgeRecord> records_{};
};

} // namespace trishula
