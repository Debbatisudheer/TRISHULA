#include "trishula/rover/science_archive.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <utility>

namespace trishula {
namespace {

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
            switch (c) {
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            default: value += c; break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') { escaped = true; continue; }
        if (c == '"') { out = value; return true; }
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
    if constexpr (std::is_same_v<T, std::uint64_t>) {
        const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), out);
        return ec == std::errc{} && ptr == token.data() + token.size();
    } else if constexpr (std::is_same_v<T, std::size_t>) {
        unsigned long long parsed = 0ULL;
        const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), parsed);
        if (ec != std::errc{} || ptr != token.data() + token.size()) return false;
        out = static_cast<std::size_t>(parsed);
        return true;
    } else {
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
}

const char* status_name(ScienceDataProductStatus status) {
    switch (status) {
    case ScienceDataProductStatus::Draft: return "Draft";
    case ScienceDataProductStatus::Validated: return "Validated";
    case ScienceDataProductStatus::Stored: return "Stored";
    case ScienceDataProductStatus::Rejected: return "Rejected";
    }
    return "Rejected";
}

bool parse_status(const std::string& value, ScienceDataProductStatus& status) {
    if (value == "Draft") status = ScienceDataProductStatus::Draft;
    else if (value == "Validated") status = ScienceDataProductStatus::Validated;
    else if (value == "Stored") status = ScienceDataProductStatus::Stored;
    else if (value == "Rejected") status = ScienceDataProductStatus::Rejected;
    else return false;
    return true;
}

void write_array(std::ostringstream& out, const char* prefix, const std::array<double, 6>& values) {
    out << "\"" << prefix << "\":[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0U) out << ",";
        out << values[i];
    }
    out << "]";
}

bool extract_array6(const std::string& line, const std::string& key, std::array<double, 6>& out) {
    const std::string needle = "\"" + key + "\":[";
    const std::size_t start = line.find(needle);
    if (start == std::string::npos) return false;
    const std::size_t value_start = start + needle.size();
    const std::size_t value_end = line.find(']', value_start);
    if (value_end == std::string::npos) return false;
    std::stringstream ss(line.substr(value_start, value_end - value_start));
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (!(ss >> out[i])) return false;
        if (i + 1U < out.size()) {
            char comma = '\0';
            if (!(ss >> comma) || comma != ',') return false;
        }
    }
    return true;
}

void write_composition(std::ostringstream& out, const char* prefix, const LunarMaterialComposition& c) {
    out << "\"" << prefix << "\":{"
        << "\"si\":" << c.si << ",\"fe\":" << c.fe << ",\"al\":" << c.al
        << ",\"ca\":" << c.ca << ",\"mg\":" << c.mg << ",\"ti\":" << c.ti << "}";
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
        const std::string n = "\"" + std::string(name) + "\":";
        const std::size_t pos = body.find(n);
        if (pos == std::string::npos) return false;
        const std::size_t vs = pos + n.size();
        const std::size_t ve = body.find(',', vs);
        const std::string token = body.substr(vs, ve == std::string::npos ? body.size() - vs : ve - vs);
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

} // namespace

ScienceDataArchive::ScienceDataArchive(std::filesystem::path root_directory)
    : root_directory_(std::move(root_directory)) {}

const std::filesystem::path& ScienceDataArchive::root_directory() const noexcept {
    return root_directory_;
}

std::filesystem::path ScienceDataArchive::product_path(const std::string& product_id) const {
    return root_directory_ / (product_id + ".json");
}

std::string ScienceDataArchive::serialize(const ScienceDataProduct& product) {
    std::ostringstream out;
    out << std::setprecision(17)
        << "{"
        << "\"product_id\":\"" << json_escape(product.metadata.product_id) << "\","
        << "\"target_id\":" << product.metadata.target_id << ","
        << "\"sequence\":" << product.metadata.sequence << ","
        << "\"instrument\":\"" << json_escape(product.metadata.instrument) << "\","
        << "\"acquisition_time_tag\":\"" << json_escape(product.metadata.acquisition_time_tag) << "\","
        << "\"provenance\":\"" << json_escape(product.metadata.provenance) << "\","
        << "\"quality\":" << product.quality << ","
        << "\"science_score\":" << product.science_score << ","
        << "\"energy_used_wh\":" << product.energy_used_wh << ","
        << "\"status\":\"" << status_name(product.status) << "\","
        << "\"acquired\":" << (product.acquired ? "true" : "false") << ","
        << "\"validated\":" << (product.validated ? "true" : "false") << ","
        << "\"stored\":" << (product.stored ? "true" : "false") << ","
        << "\"measurement\":{"
        << "\"surface_x_m\":" << product.measurement.surface_x_m << ","
        << "\"libs_laser_energy_mj\":" << product.measurement.libs_laser_energy_mj << ","
        << "\"libs_plasma_temperature_k\":" << product.measurement.libs_plasma_temperature_k << ","
        << "\"libs_signal_to_noise\":" << product.measurement.libs_signal_to_noise << ",";
    write_array(out, "libs_line_intensity", product.measurement.libs_line_intensity);
    out << ",";
    write_composition(out, "libs_inferred_abundance", product.measurement.libs_inferred_abundance);
    out << ",\"apxs_exposure_s\":" << product.measurement.apxs_exposure_s
        << ",\"apxs_counts_per_second\":" << product.measurement.apxs_counts_per_second << ",";
    write_array(out, "apxs_xray_counts", product.measurement.apxs_xray_counts);
    out << ",";
    write_composition(out, "apxs_inferred_abundance", product.measurement.apxs_inferred_abundance);
    out << ",\"camera_brightness\":" << product.measurement.camera_brightness
        << ",\"camera_texture_rms\":" << product.measurement.camera_texture_rms
        << ",\"camera_horizon_gradient\":" << product.measurement.camera_horizon_gradient
        << ",\"camera_illuminated\":" << (product.measurement.camera_illuminated ? "true" : "false")
        << "}}\n";
    return out.str();
}

bool ScienceDataArchive::deserialize(const std::string& line, ScienceDataProduct& product) {
    ScienceDataProduct parsed{};
    std::string status;
    double quality = 0.0;
    double science_score = 0.0;
    double energy_used_wh = 0.0;
    if (!extract_string(line, "product_id", parsed.metadata.product_id) ||
        !extract_number(line, "target_id", parsed.metadata.target_id) ||
        !extract_number(line, "sequence", parsed.metadata.sequence) ||
        !extract_string(line, "instrument", parsed.metadata.instrument) ||
        !extract_string(line, "acquisition_time_tag", parsed.metadata.acquisition_time_tag) ||
        !extract_string(line, "provenance", parsed.metadata.provenance) ||
        !extract_number(line, "quality", quality) ||
        !extract_number(line, "science_score", science_score) ||
        !extract_number(line, "energy_used_wh", energy_used_wh) ||
        !extract_string(line, "status", status)) {
        return false;
    }
    const auto acquired_pos = line.find("\"acquired\":");
    const auto validated_pos = line.find("\"validated\":");
    const auto stored_pos = line.find("\"stored\":");
    if (acquired_pos == std::string::npos || validated_pos == std::string::npos || stored_pos == std::string::npos) return false;
    parsed.acquired = line.compare(acquired_pos + 11U, 4U, "true") == 0;
    parsed.validated = line.compare(validated_pos + 12U, 4U, "true") == 0;
    parsed.stored = line.compare(stored_pos + 9U, 4U, "true") == 0;
    if (!parse_status(status, parsed.status)) return false;
    parsed.quality = quality;
    parsed.science_score = science_score;
    parsed.energy_used_wh = energy_used_wh;
    if (!extract_number(line, "surface_x_m", parsed.measurement.surface_x_m) ||
        !extract_number(line, "libs_laser_energy_mj", parsed.measurement.libs_laser_energy_mj) ||
        !extract_number(line, "libs_plasma_temperature_k", parsed.measurement.libs_plasma_temperature_k) ||
        !extract_number(line, "libs_signal_to_noise", parsed.measurement.libs_signal_to_noise) ||
        !extract_array6(line, "libs_line_intensity", parsed.measurement.libs_line_intensity) ||
        !extract_composition(line, "libs_inferred_abundance", parsed.measurement.libs_inferred_abundance) ||
        !extract_number(line, "apxs_exposure_s", parsed.measurement.apxs_exposure_s) ||
        !extract_number(line, "apxs_counts_per_second", parsed.measurement.apxs_counts_per_second) ||
        !extract_array6(line, "apxs_xray_counts", parsed.measurement.apxs_xray_counts) ||
        !extract_composition(line, "apxs_inferred_abundance", parsed.measurement.apxs_inferred_abundance) ||
        !extract_number(line, "camera_brightness", parsed.measurement.camera_brightness) ||
        !extract_number(line, "camera_texture_rms", parsed.measurement.camera_texture_rms) ||
        !extract_number(line, "camera_horizon_gradient", parsed.measurement.camera_horizon_gradient)) {
        return false;
    }
    const auto camera_illuminated_pos = line.find("\"camera_illuminated\":");
    if (camera_illuminated_pos == std::string::npos) return false;
    parsed.measurement.camera_illuminated = line.compare(camera_illuminated_pos + 21U, 4U, "true") == 0;
    if (parsed.metadata.product_id.empty()) return false;
    product = std::move(parsed);
    return true;
}

bool ScienceDataArchive::save(const ScienceDataProduct& product) {
    if (product.status != ScienceDataProductStatus::Stored || !product.stored || product.metadata.product_id.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(root_directory_, ec);
    if (ec) return false;
    const auto path = product_path(product.metadata.product_id);
    const auto temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << serialize(product);
        out.flush();
        if (!out) return false;
    }
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

bool ScienceDataArchive::load(const std::string& product_id, ScienceDataProduct& product) const {
    std::ifstream in(product_path(product_id), std::ios::binary);
    if (!in) return false;
    std::string line;
    std::getline(in, line);
    if (line.empty()) return false;
    return deserialize(line, product);
}

std::vector<ScienceDataProduct> ScienceDataArchive::load_all() const {
    std::vector<ScienceDataProduct> result;
    std::error_code ec;
    if (!std::filesystem::exists(root_directory_, ec) || ec) return result;
    for (const auto& entry : std::filesystem::directory_iterator(root_directory_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        if (!in) continue;
        std::string line;
        std::getline(in, line);
        ScienceDataProduct product{};
        if (!line.empty() && deserialize(line, product)) result.push_back(std::move(product));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.metadata.product_id < b.metadata.product_id;
    });
    return result;
}

std::size_t ScienceDataArchive::count() const {
    return load_all().size();
}

} // namespace trishula
