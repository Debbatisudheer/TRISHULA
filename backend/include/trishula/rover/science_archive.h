#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "trishula/rover/science_data_products.h"

namespace trishula {

class ScienceDataArchive {
public:
    explicit ScienceDataArchive(std::filesystem::path root_directory);

    [[nodiscard]] const std::filesystem::path& root_directory() const noexcept;
    [[nodiscard]] std::filesystem::path product_path(const std::string& product_id) const;

    bool save(const ScienceDataProduct& product);
    bool load(const std::string& product_id, ScienceDataProduct& product) const;
    [[nodiscard]] std::vector<ScienceDataProduct> load_all() const;
    [[nodiscard]] std::size_t count() const;

private:
    std::filesystem::path root_directory_{};

    static std::string serialize(const ScienceDataProduct& product);
    static bool deserialize(const std::string& line, ScienceDataProduct& product);
};

} // namespace trishula
