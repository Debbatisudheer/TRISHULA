#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <deque>

#include "trishula/ground/space_link.h"

namespace trishula {

enum class EventSeverity : std::uint8_t { Info = 0, Warning = 1, Error = 2, Critical = 3 };

enum class EventKind : std::uint8_t { MissionEvent = 0, Fault = 1, Recovery = 2 };

enum class EventIngestStatus {
    Accepted,
    UnsupportedPacketType,
    InvalidPayload,
    Duplicate,
    OutOfOrder,
    CapacityFull
};

struct GroundEventRecord {
    std::string source_node{};
    std::string origin_node{};
    std::string destination_node{};
    std::uint16_t application_id{0U};
    std::uint64_t mission_timestamp_ns{0U};
    std::uint32_t sequence_number{0U};
    EventKind kind{EventKind::MissionEvent};
    EventSeverity severity{EventSeverity::Info};
    std::string code{};
    std::string subsystem{};
    std::string message{};
    bool active{false};
};

struct EventIngestionConfiguration {
    std::size_t archive_capacity{4096U};
    bool reject_duplicates{true};
    bool reject_out_of_order{true};
};

struct EventIngestionStats {
    std::uint64_t packets_received{0U};
    std::uint64_t packets_accepted{0U};
    std::uint64_t unsupported_packets{0U};
    std::uint64_t invalid_payloads{0U};
    std::uint64_t duplicate_packets{0U};
    std::uint64_t out_of_order_packets{0U};
    std::uint64_t capacity_rejections{0U};
    std::uint64_t fault_events{0U};
    std::uint64_t recovery_events{0U};
};

class EventIngestionPipeline {
public:
    explicit EventIngestionPipeline(EventIngestionConfiguration configuration = {});

    [[nodiscard]] const EventIngestionConfiguration& configuration() const noexcept;
    [[nodiscard]] const EventIngestionStats& stats() const noexcept;
    [[nodiscard]] std::size_t archived_events() const noexcept;
    [[nodiscard]] std::size_t available_capacity() const noexcept;
    [[nodiscard]] double capacity_utilization() const noexcept;
    [[nodiscard]] std::size_t active_faults() const noexcept;

    EventIngestStatus ingest(const GroundPacket& packet, std::string& error);
    bool pop_event(GroundEventRecord& event);
    bool get_active_fault(const std::string& origin_node, const std::string& code, GroundEventRecord& event) const;
    bool clear_fault(const std::string& origin_node, const std::string& code);
    void clear_archive() noexcept;
    void reset_statistics() noexcept;

private:
    struct StreamKey {
        std::string source_node{};
        std::string origin_node{};
        std::uint16_t application_id{0U};
        bool operator==(const StreamKey& other) const noexcept {
            return source_node == other.source_node && origin_node == other.origin_node && application_id == other.application_id;
        }
    };
    struct StreamKeyHash {
        std::size_t operator()(const StreamKey& key) const noexcept;
    };
    struct FaultKey {
        std::string origin_node{};
        std::string code{};
        bool operator==(const FaultKey& other) const noexcept {
            return origin_node == other.origin_node && code == other.code;
        }
    };
    struct FaultKeyHash {
        std::size_t operator()(const FaultKey& key) const noexcept;
    };

    static bool parse_payload(const GroundPacket& packet, GroundEventRecord& event, std::string& error);
    static std::string trim(const std::string& value);
    static StreamKey stream_key(const GroundPacket& packet);

    EventIngestionConfiguration configuration_{};
    EventIngestionStats stats_{};
    std::deque<GroundEventRecord> archive_{};
    std::unordered_map<StreamKey, std::uint32_t, StreamKeyHash> last_sequence_by_stream_{};
    std::unordered_map<FaultKey, GroundEventRecord, FaultKeyHash> active_faults_{};
};

} // namespace trishula
