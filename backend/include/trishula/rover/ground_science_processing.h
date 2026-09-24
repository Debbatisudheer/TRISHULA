#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "trishula/rover/science_archive.h"

namespace trishula {

struct GroundScienceAnalysis {
    std::string product_id{};
    std::size_t target_id{0U};
    std::string provenance{};
    double input_quality{0.0};
    double libs_apxs_abundance_agreement{0.0};
    double derived_fe_mg_ratio{0.0};
    double derived_ti_si_ratio{0.0};
    LunarMaterialComposition fused_abundance{};
    std::string interpretation{};
    bool scientifically_usable{false};
};

class GroundScienceProcessor {
public:
    explicit GroundScienceProcessor(std::filesystem::path analysis_directory);

    [[nodiscard]] const std::filesystem::path& analysis_directory() const noexcept;
    [[nodiscard]] std::filesystem::path analysis_path(const std::string& product_id) const;

    bool process_product(const ScienceDataProduct& product, GroundScienceAnalysis& analysis) const;
    bool process_archive(const ScienceDataArchive& ground_archive,
                         std::vector<GroundScienceAnalysis>& analyses) const;
    bool save(const GroundScienceAnalysis& analysis) const;
    bool load(const std::string& product_id, GroundScienceAnalysis& analysis) const;

private:
    std::filesystem::path analysis_directory_{};

    static std::string serialize(const GroundScienceAnalysis& analysis);
    static bool deserialize(const std::string& line, GroundScienceAnalysis& analysis);
};

} // namespace trishula
