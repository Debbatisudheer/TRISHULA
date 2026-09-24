#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <deque>

#include "trishula/ground/space_link.h"

namespace trishula {

enum class TelemetryIngestStatus { Accepted, UnsupportedPacketType, InvalidPayload, Duplicate, OutOfOrder, CapacityFull };

struct TelemetryRecord {
    std::string source_node{};
    std::string origin_node{};
    std::string destination_node{};
    std::uint16_t application_id{0U};
    std::uint64_t mission_timestamp_ns{0U};
    std::uint32_t sequence_number{0U};
    std::string subsystem{};
    std::string metric{};
    double value{0.0};
    std::string unit{};
    double quality{1.0};
};

struct TelemetryIngestionConfiguration {
    std::size_t archive_capacity{4096U};
    bool reject_duplicates{true};
    bool reject_out_of_order{true};
};

struct TelemetryIngestionStats {
    std::uint64_t packets_received{0U};
    std::uint64_t packets_accepted{0U};
    std::uint64_t unsupported_packets{0U};
    std::uint64_t invalid_payloads{0U};
    std::uint64_t duplicate_packets{0U};
    std::uint64_t out_of_order_packets{0U};
    std::uint64_t capacity_rejections{0U};
};

class TelemetryIngestionPipeline {
public:
    explicit TelemetryIngestionPipeline(TelemetryIngestionConfiguration configuration = {});

    [[nodiscard]] const TelemetryIngestionConfiguration& configuration() const noexcept;
    [[nodiscard]] const TelemetryIngestionStats& stats() const noexcept;
    [[nodiscard]] std::size_t archived_records() const noexcept;
    [[nodiscard]] std::size_t available_capacity() const noexcept;
    [[nodiscard]] double capacity_utilization() const noexcept;

    TelemetryIngestStatus ingest(const GroundPacket& packet, std::string& error);
    bool pop_record(TelemetryRecord& record);
    [[nodiscard]] bool get_current(const std::string& source_node, const std::string& origin_node,
                                   const std::string& metric, TelemetryRecord& record) const;
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
    struct MetricKey {
        std::string source_node{};
        std::string origin_node{};
        std::string metric{};
        bool operator==(const MetricKey& other) const noexcept {
            return source_node == other.source_node && origin_node == other.origin_node && metric == other.metric;
        }
    };
    struct MetricKeyHash {
        std::size_t operator()(const MetricKey& key) const noexcept;
    };

    static bool parse_payload(const GroundPacket& packet, TelemetryRecord& record, std::string& error);
    static std::string trim(const std::string& value);
    static StreamKey stream_key(const GroundPacket& packet);

    TelemetryIngestionConfiguration configuration_{};
    TelemetryIngestionStats stats_{};
    std::deque<TelemetryRecord> archive_{};
    std::unordered_map<StreamKey, std::uint32_t, StreamKeyHash> last_sequence_by_stream_{};
    std::unordered_map<MetricKey, TelemetryRecord, MetricKeyHash> current_state_{};
};

} // namespace trishula
