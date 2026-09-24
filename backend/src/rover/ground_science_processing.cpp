#include "trishula/rover/ground_science_processing.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace trishula {
namespace {

constexpr double kEpsilon = 1.0e-12;

std::string json_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8U);
    for (const char c : value) {
        switch (c) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += c; break;
        }
    }
    return result;
}

bool extract_string(const std::string& line, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\":\"";
    const std::size_t start = line.find(needle);
    if (start == std::string::npos) return false;
    const std::size_t value_start = start + needle.size();
    std::string value;
    bool escaped = false;
    for (std::size_t i = value_start; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) {
            if (c == 'n') value += '\n';
            else if (c == 'r') value += '\r';
            else if (c == 't') value += '\t';
            else value += c;
            escaped = false;
            continue;
        }
        if (c == '\\') { escaped = true; continue; }
        if (c == '"') { out = std::move(value); return true; }
        value += c;
    }
    return false;
}

template <typename T>
bool extract_number(const std::string& line, const std::string& key, T& out) {
    const std::string needle = "\"" + key + "\":";
    const std::size_t start = line.find(needle);
    if (start == std::string::npos) return false;
    const std::size_t value_start = start + needle.size();
    const std::size_t value_end = line.find_first_of(",}", value_start);
    if (value_end == std::string::npos) return false;
    const std::string token = line.substr(value_start, value_end - value_start);
    try {
        std::size_t consumed = 0U;
        const double parsed = std::stod(token, &consumed);
        if (consumed != token.size()) return false;
        out = static_cast<T>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

void write_composition(std::ostringstream& out, const LunarMaterialComposition& c) {
    out << "{\"si\":" << c.si << ",\"fe\":" << c.fe
        << ",\"al\":" << c.al << ",\"ca\":" << c.ca
        << ",\"mg\":" << c.mg << ",\"ti\":" << c.ti << "}";
}

bool extract_composition(const std::string& line, const std::string& key, LunarMaterialComposition& c) {
    const std::string needle = "\"" + key + "\":{";
    const std::size_t start = line.find(needle);
    if (start == std::string::npos) return false;
    const std::size_t value_start = start + needle.size();
    const std::size_t value_end = line.find('}', value_start);
    if (value_end == std::string::npos) return false;
    const std::string body = line.substr(value_start, value_end - value_start);

    auto get = [&body](const char* name, double& value) -> bool {
        const std::string needle_inner = "\"" + std::string(name) + "\":";
        const std::size_t pos = body.find(needle_inner);
        if (pos == std::string::npos) return false;
        const std::size_t value_start_inner = pos + needle_inner.size();
        const std::size_t value_end_inner = body.find(',', value_start_inner);
        const std::string token = body.substr(value_start_inner,
            value_end_inner == std::string::npos ? body.size() - value_start_inner : value_end_inner - value_start_inner);
        try {
            std::size_t consumed = 0U;
            value = std::stod(token, &consumed);
            return consumed == token.size();
        } catch (...) {
            return false;
        }
    };
    return get("si", c.si) && get("fe", c.fe) && get("al", c.al) &&
           get("ca", c.ca) && get("mg", c.mg) && get("ti", c.ti);
}

LunarMaterialComposition weighted_fuse(const LunarMaterialComposition& libs,
                                       const LunarMaterialComposition& apxs) {
    LunarMaterialComposition fused{
        0.5 * (libs.si + apxs.si),
        0.5 * (libs.fe + apxs.fe),
        0.5 * (libs.al + apxs.al),
        0.5 * (libs.ca + apxs.ca),
        0.5 * (libs.mg + apxs.mg),
        0.5 * (libs.ti + apxs.ti)};
    const double sum = fused.si + fused.fe + fused.al + fused.ca + fused.mg + fused.ti;
    if (sum > kEpsilon) {
        fused.si /= sum; fused.fe /= sum; fused.al /= sum;
        fused.ca /= sum; fused.mg /= sum; fused.ti /= sum;
    }
    return fused;
}

}

GroundScienceProcessor::GroundScienceProcessor(std::filesystem::path analysis_directory)
    : analysis_directory_(std::move(analysis_directory)) {}

const std::filesystem::path& GroundScienceProcessor::analysis_directory() const noexcept {
    return analysis_directory_;
}

std::filesystem::path GroundScienceProcessor::analysis_path(const std::string& product_id) const {
    return analysis_directory_ / (product_id + ".analysis.json");
}

bool GroundScienceProcessor::process_product(const ScienceDataProduct& product,
                                             GroundScienceAnalysis& analysis) const {
    analysis = {};
    analysis.product_id = product.metadata.product_id;
    analysis.target_id = product.metadata.target_id;
    analysis.provenance = product.metadata.provenance;
    analysis.input_quality = product.quality;

    if (product.status != ScienceDataProductStatus::Stored || !product.validated || !product.stored) {
        return false;
    }

    const auto& libs = product.measurement.libs_inferred_abundance;
    const auto& apxs = product.measurement.apxs_inferred_abundance;
    const double differences[] = {
        libs.si - apxs.si, libs.fe - apxs.fe, libs.al - apxs.al,
        libs.ca - apxs.ca, libs.mg - apxs.mg, libs.ti - apxs.ti};
    double squared_error = 0.0;
    for (const double difference : differences) squared_error += difference * difference;
    analysis.libs_apxs_abundance_agreement = std::sqrt(squared_error / 6.0);
    analysis.fused_abundance = weighted_fuse(libs, apxs);
    analysis.derived_fe_mg_ratio = analysis.fused_abundance.fe / std::max(analysis.fused_abundance.mg, kEpsilon);
    analysis.derived_ti_si_ratio = analysis.fused_abundance.ti / std::max(analysis.fused_abundance.si, kEpsilon);

    const bool instruments_present = product.measurement.libs_signal_to_noise > 0.0 &&
                                     product.measurement.apxs_counts_per_second > 0.0 &&
                                     product.measurement.camera_illuminated;
    const bool quality_ok = product.quality >= 0.70;
    const bool agreement_ok = analysis.libs_apxs_abundance_agreement <= 1.0e-9;
    analysis.scientifically_usable = quality_ok && agreement_ok && instruments_present;

    if (analysis.derived_ti_si_ratio > 0.14 && analysis.fused_abundance.fe > 0.18) {
        analysis.interpretation = "Measured composition is relatively Ti/Fe enriched within the simulator's analysis thresholds.";
    } else if (analysis.fused_abundance.si > 0.34 && analysis.fused_abundance.al > 0.14) {
        analysis.interpretation = "Measured composition is relatively Si/Al enriched within the simulator's analysis thresholds.";
    } else {
        analysis.interpretation = "Measured composition is within the baseline simulator classification range.";
    }
    return analysis.scientifically_usable;
}

bool GroundScienceProcessor::process_archive(const ScienceDataArchive& ground_archive,
                                              std::vector<GroundScienceAnalysis>& analyses) const {
    analyses.clear();
    for (const auto& product : ground_archive.load_all()) {
        GroundScienceAnalysis analysis{};
        if (!process_product(product, analysis)) return false;
        analyses.push_back(std::move(analysis));
    }
    return !analyses.empty();
}

std::string GroundScienceProcessor::serialize(const GroundScienceAnalysis& analysis) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{\"product_id\":\"" << json_escape(analysis.product_id) << "\","
        << "\"target_id\":" << analysis.target_id << ","
        << "\"provenance\":\"" << json_escape(analysis.provenance) << "\","
        << "\"input_quality\":" << analysis.input_quality << ","
        << "\"libs_apxs_abundance_agreement\":" << analysis.libs_apxs_abundance_agreement << ","
        << "\"derived_fe_mg_ratio\":" << analysis.derived_fe_mg_ratio << ","
        << "\"derived_ti_si_ratio\":" << analysis.derived_ti_si_ratio << ","
        << "\"fused_abundance\":";
    write_composition(out, analysis.fused_abundance);
    out << ",\"interpretation\":\"" << json_escape(analysis.interpretation) << "\","
        << "\"scientifically_usable\":" << (analysis.scientifically_usable ? "true" : "false") << "}\n";
    return out.str();
}

bool GroundScienceProcessor::deserialize(const std::string& line, GroundScienceAnalysis& analysis) {
    GroundScienceAnalysis parsed{};
    if (!extract_string(line, "product_id", parsed.product_id) ||
        !extract_number(line, "target_id", parsed.target_id) ||
        !extract_string(line, "provenance", parsed.provenance) ||
        !extract_number(line, "input_quality", parsed.input_quality) ||
        !extract_number(line, "libs_apxs_abundance_agreement", parsed.libs_apxs_abundance_agreement) ||
        !extract_number(line, "derived_fe_mg_ratio", parsed.derived_fe_mg_ratio) ||
        !extract_number(line, "derived_ti_si_ratio", parsed.derived_ti_si_ratio) ||
        !extract_composition(line, "fused_abundance", parsed.fused_abundance) ||
        !extract_string(line, "interpretation", parsed.interpretation)) return false;
    const std::string needle = "\"scientifically_usable\":";
    const std::size_t pos = line.find(needle);
    if (pos == std::string::npos) return false;
    parsed.scientifically_usable = line.compare(pos + needle.size(), 4U, "true") == 0;
    analysis = std::move(parsed);
    return true;
}

bool GroundScienceProcessor::save(const GroundScienceAnalysis& analysis) const {
    if (analysis.product_id.empty() || !analysis.scientifically_usable) return false;
    std::error_code ec;
    std::filesystem::create_directories(analysis_directory_, ec);
    if (ec) return false;
    const auto final_path = analysis_path(analysis.product_id);
    const auto temp_path = final_path.string() + ".tmp";
    {
        std::ofstream out(temp_path, std::ios::trunc);
        if (!out) return false;
        out << serialize(analysis);
        out.flush();
        if (!out) return false;
    }
    std::filesystem::rename(temp_path, final_path, ec);
    if (ec) {
        std::filesystem::remove(final_path, ec);
        ec.clear();
        std::filesystem::rename(temp_path, final_path, ec);
    }
    if (ec) {
        std::filesystem::remove(temp_path, ec);
        return false;
    }
    return true;
}

bool GroundScienceProcessor::load(const std::string& product_id, GroundScienceAnalysis& analysis) const {
    std::ifstream in(analysis_path(product_id));
    if (!in) return false;
    std::string line;
    std::getline(in, line);
    return !line.empty() && deserialize(line, analysis);
}

} // namespace trishula
