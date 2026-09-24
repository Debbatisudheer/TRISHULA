#include "trishula/rover/science_knowledge.h"

#include <algorithm>

namespace trishula {

bool MissionKnowledgeBase::ingest(const ScienceDataProduct& product) {
    if (product.status != ScienceDataProductStatus::Stored || !product.stored) return false;
    auto it = std::find_if(records_.begin(), records_.end(), [&](const auto& record) {
        return record.target_id == product.metadata.target_id;
    });
    if (it == records_.end()) {
        MissionKnowledgeRecord record{};
        record.target_id = product.metadata.target_id;
        record.best_quality = product.quality;
        record.cumulative_science_score = product.science_score;
        record.cumulative_energy_used_wh = product.energy_used_wh;
        record.observation_count = 1U;
        record.high_quality = product.quality >= 0.85;
        record.latest_product_id = product.metadata.product_id;
        records_.push_back(record);
    } else {
        it->best_quality = std::max(it->best_quality, product.quality);
        it->cumulative_science_score += product.science_score;
        it->cumulative_energy_used_wh += product.energy_used_wh;
        ++it->observation_count;
        it->high_quality = it->best_quality >= 0.85;
        it->latest_product_id = product.metadata.product_id;
    }
    return true;
}

const MissionKnowledgeRecord* MissionKnowledgeBase::find(std::size_t target_id) const {
    const auto it = std::find_if(records_.begin(), records_.end(), [&](const auto& record) {
        return record.target_id == target_id;
    });
    return it == records_.end() ? nullptr : &(*it);
}

std::vector<MissionKnowledgeRecord> MissionKnowledgeBase::high_quality_records(double threshold) const {
    std::vector<MissionKnowledgeRecord> result;
    for (const auto& record : records_) {
        if (record.best_quality >= threshold) result.push_back(record);
    }
    return result;
}

std::size_t MissionKnowledgeBase::size() const noexcept { return records_.size(); }

} // namespace trishula
