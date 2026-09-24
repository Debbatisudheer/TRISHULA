#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>

#include "trishula/ground/space_link.h"

namespace trishula {

enum class CommandIngestStatus {
    Accepted,
    UnsupportedPacketType,
    InvalidPayload,
    Duplicate,
    OutOfOrder,
    CapacityFull,
    InvalidTarget,
    UnknownOpcode
};

struct GroundCommandRecord {
    std::string command_id{};
    std::string source_node{};
    std::string destination_node{};
    std::string target{};
    std::string opcode{};
    std::string parameters{};
    GroundPacketPriority priority{GroundPacketPriority::Normal};
    std::uint16_t application_id{0U};
    std::uint64_t mission_timestamp_ns{0U};
    std::uint32_t sequence_number{0U};
};

struct CommandIngestionConfiguration {
    std::size_t archive_capacity{1024U};
    bool reject_duplicates{true};
    bool reject_out_of_order{true};
    bool require_non_empty_parameters{false};
    std::unordered_set<std::string> allowed_targets{"VIKRAM", "ROVER", "LANDER"};
    std::unordered_set<std::string> allowed_opcodes{
        "SET_MODE", "START_ROVER", "STOP_ROVER", "DRIVE", "STOP",
        "START_SCIENCE", "STOP_SCIENCE", "TRANSMIT_DATA", "RESET_SUBSYSTEM", "ENTER_SAFE_MODE", "EXIT_SAFE_MODE",
        "COAST", "MAIN_ENGINE", "RCS"
    };
};

struct CommandIngestionStats {
    std::uint64_t packets_received{0U};
    std::uint64_t packets_accepted{0U};
    std::uint64_t unsupported_packets{0U};
    std::uint64_t invalid_payloads{0U};
    std::uint64_t duplicate_packets{0U};
    std::uint64_t out_of_order_packets{0U};
    std::uint64_t capacity_rejections{0U};
    std::uint64_t invalid_targets{0U};
    std::uint64_t unknown_opcodes{0U};
};

class CommandIngestionPipeline {
public:
    explicit CommandIngestionPipeline(CommandIngestionConfiguration configuration = {});

    [[nodiscard]] const CommandIngestionConfiguration& configuration() const noexcept;
    [[nodiscard]] const CommandIngestionStats& stats() const noexcept;
    [[nodiscard]] std::size_t archived_commands() const noexcept;
    [[nodiscard]] std::size_t available_capacity() const noexcept;
    [[nodiscard]] double capacity_utilization() const noexcept;

    CommandIngestStatus ingest(const GroundPacket& packet, std::string& error);
    bool pop_command(GroundCommandRecord& command);
    [[nodiscard]] bool contains_command_id(const std::string& command_id) const noexcept;
    void clear_archive() noexcept;
    void reset_statistics() noexcept;

private:
    struct StreamKey {
        std::string source_node{};
        std::string destination_node{};
        std::uint16_t application_id{0U};
        bool operator==(const StreamKey& other) const noexcept {
            return source_node == other.source_node &&
                   destination_node == other.destination_node &&
                   application_id == other.application_id;
        }
    };

    struct StreamKeyHash {
        std::size_t operator()(const StreamKey& key) const noexcept;
    };

    bool parse_payload(const GroundPacket& packet, GroundCommandRecord& command, std::string& error) const;
    static std::string trim(const std::string& value);
    static bool valid_identifier(const std::string& value) noexcept;
    static std::string uppercase(std::string value);
    static StreamKey stream_key(const GroundPacket& packet);

    CommandIngestionConfiguration configuration_{};
    CommandIngestionStats stats_{};
    std::deque<GroundCommandRecord> archive_{};
    std::unordered_map<StreamKey, std::uint32_t, StreamKeyHash> last_sequence_by_stream_{};
    std::unordered_set<std::string> command_ids_{};
};

} // namespace trishula
