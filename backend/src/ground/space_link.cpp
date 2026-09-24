#include "trishula/ground/space_link.h"

#include <limits>
#include <utility>

namespace trishula {
namespace {
constexpr std::uint16_t kPacketMagic = 0x5452U;
constexpr std::size_t kMaxNodeIdLength = 255U;
constexpr std::size_t kMaxPayloadLength = 16U * 1024U * 1024U;

void put8(std::vector<std::uint8_t>& b, std::uint8_t v) { b.push_back(v); }
void put16(std::vector<std::uint8_t>& b, std::uint16_t v) { b.push_back(static_cast<std::uint8_t>(v >> 8U)); b.push_back(static_cast<std::uint8_t>(v)); }
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) { for (int s=24;s>=0;s-=8) b.push_back(static_cast<std::uint8_t>(v >> s)); }
void put64(std::vector<std::uint8_t>& b, std::uint64_t v) { for (int s=56;s>=0;s-=8) b.push_back(static_cast<std::uint8_t>(v >> s)); }

bool get8(const std::vector<std::uint8_t>& b, std::size_t& o, std::uint8_t& v) { if(o>=b.size()) return false; v=b[o++]; return true; }
bool get16(const std::vector<std::uint8_t>& b, std::size_t& o, std::uint16_t& v) { if(b.size()-o<2U) return false; v=static_cast<std::uint16_t>((static_cast<std::uint16_t>(b[o])<<8U)|b[o+1U]); o+=2U; return true; }
bool get32(const std::vector<std::uint8_t>& b, std::size_t& o, std::uint32_t& v) { if(b.size()-o<4U) return false; v=0U; for(int i=0;i<4;i++) v=(v<<8U)|b[o++]; return true; }
bool get64(const std::vector<std::uint8_t>& b, std::size_t& o, std::uint64_t& v) { if(b.size()-o<8U) return false; v=0U; for(int i=0;i<8;i++) v=(v<<8U)|b[o++]; return true; }

bool put_string(std::vector<std::uint8_t>& b, const std::string& s, std::string& e) {
    if(s.size()>kMaxNodeIdLength || s.size()>std::numeric_limits<std::uint8_t>::max()){ e="node identifier exceeds 255 bytes"; return false; }
    put8(b, static_cast<std::uint8_t>(s.size())); b.insert(b.end(),s.begin(),s.end()); return true;
}
bool get_string(const std::vector<std::uint8_t>& b, std::size_t& o, std::string& s) {
    std::uint8_t n=0U; if(!get8(b,o,n) || b.size()-o<n) return false; s.assign(reinterpret_cast<const char*>(b.data()+o),n); o+=n; return true;
}
bool valid_header(const SpaceLinkPacketHeader& h) noexcept {
    const auto t=static_cast<std::uint8_t>(h.type), p=static_cast<std::uint8_t>(h.priority);
    return h.version!=0U && t>=1U && t<=5U && p<=3U;
}
}

std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) noexcept {
    std::uint32_t crc=0xFFFFFFFFU;
    for(const auto byte:bytes){
        crc^=byte;
        for(int bit=0;bit<8;bit++){
            const std::uint32_t mask=static_cast<std::uint32_t>(-(static_cast<std::int32_t>(crc&1U)));
            crc=(crc>>1U)^(0xEDB88320U&mask);
        }
    }
    return crc^0xFFFFFFFFU;
}

bool encode_packet(const GroundPacket& packet, std::vector<std::uint8_t>& bytes, std::string& error) {
    bytes.clear(); error.clear(); const auto& h=packet.header;
    if(!valid_header(h)){ error="invalid packet header"; return false; }
    if(packet.payload.size()>kMaxPayloadLength){ error="payload exceeds ground packet limit"; return false; }
    bytes.reserve(32U+h.source_node.size()+h.origin_node.size()+h.destination_node.size()+packet.payload.size());
    put16(bytes,kPacketMagic); put8(bytes,h.version); put8(bytes,static_cast<std::uint8_t>(h.type)); put8(bytes,static_cast<std::uint8_t>(h.priority));
    if(!put_string(bytes,h.source_node,error)||!put_string(bytes,h.origin_node,error)||!put_string(bytes,h.destination_node,error)) return false;
    put16(bytes,h.application_id); put64(bytes,h.mission_timestamp_ns); put32(bytes,h.sequence_number); put32(bytes,static_cast<std::uint32_t>(packet.payload.size()));
    bytes.insert(bytes.end(),packet.payload.begin(),packet.payload.end()); return true;
}

bool decode_packet(const std::vector<std::uint8_t>& bytes, GroundPacket& packet, std::string& error) {
    packet={}; error.clear(); std::size_t o=0U; std::uint16_t magic=0U;
    if(!get16(bytes,o,magic)||magic!=kPacketMagic){error="invalid packet magic";return false;}
    SpaceLinkPacketHeader h{}; std::uint8_t t=0U,p=0U;
    if(!get8(bytes,o,h.version)||!get8(bytes,o,t)||!get8(bytes,o,p)||!get_string(bytes,o,h.source_node)||!get_string(bytes,o,h.origin_node)||!get_string(bytes,o,h.destination_node)||!get16(bytes,o,h.application_id)||!get64(bytes,o,h.mission_timestamp_ns)||!get32(bytes,o,h.sequence_number)){error="truncated packet header";return false;}
    std::uint32_t len=0U; if(!get32(bytes,o,len)||len>kMaxPayloadLength||bytes.size()-o!=len){error="invalid packet payload length";return false;}
    h.type=static_cast<GroundPacketType>(t); h.priority=static_cast<GroundPacketPriority>(p); if(!valid_header(h)){error="invalid packet header values";return false;}
    packet.header=std::move(h); packet.payload.assign(bytes.begin()+static_cast<std::ptrdiff_t>(o),bytes.end()); return true;
}

bool encode_frame(const GroundPacket& packet,std::uint32_t frame_sequence,SpaceLinkFrame& frame,std::string& error){
    frame={}; frame.version=1U; frame.frame_sequence=frame_sequence; if(!encode_packet(packet,frame.packet_bytes,error)) return false; frame.crc32=crc32(frame.packet_bytes); return true;
}

bool validate_frame(const SpaceLinkFrame& frame) noexcept { return frame.version==1U && crc32(frame.packet_bytes)==frame.crc32; }

std::size_t GroundStation::StreamKeyHash::operator()(const StreamKey& key) const noexcept {
    const auto h1=std::hash<std::string>{}(key.source_node), h2=std::hash<std::string>{}(key.origin_node), h3=std::hash<std::uint16_t>{}(key.application_id);
    return h1^(h2<<1U)^(h3<<2U);
}
GroundStation::GroundStation(GroundStationConfiguration configuration):configuration_(std::move(configuration)){receive_queue_.reserve(configuration_.receive_queue_capacity);}
const GroundStationConfiguration& GroundStation::configuration() const noexcept{return configuration_;}
const GroundStationStats& GroundStation::stats() const noexcept{return stats_;}
std::size_t GroundStation::pending_packets() const noexcept{return receive_queue_.size();}
GroundStation::StreamKey GroundStation::stream_key(const GroundPacket& packet){return {packet.header.source_node,packet.header.origin_node,packet.header.application_id};}
bool GroundStation::sequence_is_duplicate(const std::unordered_map<StreamKey,std::uint32_t,StreamKeyHash>& s,const StreamKey& k,std::uint32_t n) noexcept{const auto it=s.find(k);return it!=s.end()&&it->second==n;}

GroundIngestStatus GroundStation::ingest_frame(const SpaceLinkFrame& frame,std::string& error){
    error.clear(); ++stats_.frames_received;
    if(!validate_frame(frame)){++stats_.frames_rejected;++stats_.crc_failures;error="frame CRC/version validation failed";return GroundIngestStatus::InvalidFrame;}
    GroundPacket p{}; if(!decode_packet(frame.packet_bytes,p,error)){++stats_.frames_rejected;++stats_.invalid_packets;return GroundIngestStatus::InvalidPacket;} ++stats_.packets_decoded;
    const StreamKey key=stream_key(p); const auto it=last_sequence_by_stream_.find(key);
    if(sequence_is_duplicate(last_sequence_by_stream_,key,p.header.sequence_number)){++stats_.duplicate_packets;if(configuration_.reject_duplicates)return GroundIngestStatus::Duplicate;}
    if(it!=last_sequence_by_stream_.end()&&p.header.sequence_number<it->second){++stats_.out_of_order_packets;if(configuration_.reject_out_of_order)return GroundIngestStatus::OutOfOrder;}
    if(receive_queue_.size()>=configuration_.receive_queue_capacity){++stats_.queue_overflows;return GroundIngestStatus::QueueFull;}
    receive_queue_.push_back(std::move(p)); last_sequence_by_stream_[key]=receive_queue_.back().header.sequence_number; ++stats_.packets_accepted;
    return GroundIngestStatus::Accepted;
}

bool GroundStation::pop_packet(GroundPacket& packet){if(receive_queue_.empty())return false;packet=std::move(receive_queue_.front());receive_queue_.erase(receive_queue_.begin());return true;}
void GroundStation::reset_statistics() noexcept{stats_={};}
void GroundStation::clear_receive_queue() noexcept{receive_queue_.clear();}

} // namespace trishula
