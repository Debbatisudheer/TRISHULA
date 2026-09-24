#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "trishula/rover/science_data_products.h"
#include "trishula/rover/science_archive.h"

namespace trishula {

enum class RelayNode { Rover, Vikram, Ground }; 

struct ScienceRelayPacket {
    std::string packet_id{};
    std::string product_id{};
    RelayNode source{RelayNode::Rover};
    RelayNode destination{RelayNode::Vikram};
    std::uint64_t sequence{0U};
    std::uint32_t checksum{0U};
};

struct ScienceRelayResult {
    bool rover_to_vikram{false};
    bool vikram_to_ground{false};
    bool checksum_valid{false};
    bool duplicate_rejected{false};
    bool ground_archive_persisted{false};
};

class ScienceDataRelay {
public:
    ScienceDataRelay(std::filesystem::path rover_archive,
                     std::filesystem::path vikram_archive,
                     std::filesystem::path ground_archive);

    ScienceRelayResult transmit(const std::string& product_id) const;
    [[nodiscard]] const std::filesystem::path& rover_archive() const noexcept;
    [[nodiscard]] const std::filesystem::path& vikram_archive() const noexcept;
    [[nodiscard]] const std::filesystem::path& ground_archive() const noexcept;

private:
    std::filesystem::path rover_archive_{};
    std::filesystem::path vikram_archive_{};
    std::filesystem::path ground_archive_{};

    static std::uint32_t checksum(const ScienceDataProduct& product);
};

} // namespace trishula
