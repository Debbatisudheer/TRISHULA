#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace trishula {

enum class GroundPacketType : std::uint8_t { Telemetry = 1, Science = 2, Event = 3, Command = 4, File = 5 };
enum class GroundPacketPriority : std::uint8_t { Low = 0, Normal = 1, High = 2, Critical = 3 };

struct SpaceLinkPacketHeader {
    std::uint8_t version{1U};
    GroundPacketType type{GroundPacketType::Telemetry};
    GroundPacketPriority priority{GroundPacketPriority::Normal};
    std::string source_node{};
    std::string origin_node{};
    std::string destination_node{};
    std::uint16_t application_id{0U};
    std::uint64_t mission_timestamp_ns{0U};
    std::uint32_t sequence_number{0U};
};

struct GroundPacket { SpaceLinkPacketHeader header{}; std::vector<std::uint8_t> payload{}; };
struct SpaceLinkFrame { std::uint16_t version{1U}; std::uint32_t frame_sequence{0U}; std::vector<std::uint8_t> packet_bytes{}; std::uint32_t crc32{0U}; };

enum class GroundIngestStatus { Accepted, Duplicate, OutOfOrder, InvalidFrame, InvalidPacket, QueueFull };

struct GroundStationConfiguration {
    std::string station_id{"GS-TRISHULA-01"};
    std::size_t receive_queue_capacity{1024U};
    bool reject_duplicates{true};
    bool reject_out_of_order{false};
};

struct GroundStationStats {
    std::uint64_t frames_received{0U};
    std::uint64_t frames_rejected{0U};
    std::uint64_t crc_failures{0U};
    std::uint64_t packets_decoded{0U};
    std::uint64_t packets_accepted{0U};
    std::uint64_t duplicate_packets{0U};
    std::uint64_t out_of_order_packets{0U};
    std::uint64_t invalid_packets{0U};
    std::uint64_t queue_overflows{0U};
};

[[nodiscard]] std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) noexcept;
[[nodiscard]] bool encode_packet(const GroundPacket& packet, std::vector<std::uint8_t>& bytes, std::string& error);
[[nodiscard]] bool decode_packet(const std::vector<std::uint8_t>& bytes, GroundPacket& packet, std::string& error);
[[nodiscard]] bool encode_frame(const GroundPacket& packet, std::uint32_t frame_sequence, SpaceLinkFrame& frame, std::string& error);
[[nodiscard]] bool validate_frame(const SpaceLinkFrame& frame) noexcept;

class GroundStation {
public:
    explicit GroundStation(GroundStationConfiguration configuration = {});
    [[nodiscard]] const GroundStationConfiguration& configuration() const noexcept;
    [[nodiscard]] const GroundStationStats& stats() const noexcept;
    [[nodiscard]] std::size_t pending_packets() const noexcept;
    GroundIngestStatus ingest_frame(const SpaceLinkFrame& frame, std::string& error);
    bool pop_packet(GroundPacket& packet);
    void reset_statistics() noexcept;
    void clear_receive_queue() noexcept;
private:
    struct StreamKey {
        std::string source_node{};
        std::string origin_node{};
        std::uint16_t application_id{0U};
        bool operator==(const StreamKey& other) const noexcept {
            return source_node == other.source_node && origin_node == other.origin_node && application_id == other.application_id;
        }
    };
    struct StreamKeyHash { std::size_t operator()(const StreamKey& key) const noexcept; };
    static StreamKey stream_key(const GroundPacket& packet);
    static bool sequence_is_duplicate(const std::unordered_map<StreamKey, std::uint32_t, StreamKeyHash>& state,
                                      const StreamKey& key, std::uint32_t sequence) noexcept;
    GroundStationConfiguration configuration_{};
    GroundStationStats stats_{};
    std::vector<GroundPacket> receive_queue_{};
    std::unordered_map<StreamKey, std::uint32_t, StreamKeyHash> last_sequence_by_stream_{};
};

} // namespace trishula
