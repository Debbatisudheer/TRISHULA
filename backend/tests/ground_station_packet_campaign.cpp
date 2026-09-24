#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "trishula/ground/space_link.h"

namespace {
using namespace trishula;
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
GroundPacket make_packet(GroundPacketType type,std::uint32_t seq,const std::string& payload){
    GroundPacket p{}; p.header.type=type; p.header.priority=(type==GroundPacketType::Event?GroundPacketPriority::Critical:GroundPacketPriority::Normal);
    p.header.source_node="VIKRAM"; p.header.origin_node=(type==GroundPacketType::Science?"ROVER":"VIKRAM"); p.header.destination_node="GS-TRISHULA-01";
    p.header.application_id=static_cast<std::uint16_t>(100U+static_cast<std::uint8_t>(type)); p.header.mission_timestamp_ns=123456789000ULL+seq; p.header.sequence_number=seq;
    p.payload.assign(payload.begin(),payload.end()); return p;
}
void test_round_trip(){
    const auto original=make_packet(GroundPacketType::Science,42U,"LIBS|SNR=42|Ti=0.21"); std::vector<std::uint8_t> encoded{}; std::string error;
    require(encode_packet(original,encoded,error),"packet encoding failed"); GroundPacket decoded{}; require(decode_packet(encoded,decoded,error),"packet decoding failed");
    require(decoded.header.source_node=="VIKRAM","source node was not preserved"); require(decoded.header.origin_node=="ROVER","origin node was not preserved");
    require(decoded.header.sequence_number==42U,"sequence number was not preserved"); require(decoded.payload==original.payload,"payload was not preserved");
}
void test_ingest(){
    GroundStation station(GroundStationConfiguration{"GS-TRISHULA-01",8U,true,true}); std::string error; SpaceLinkFrame frame{};
    require(encode_frame(make_packet(GroundPacketType::Telemetry,1U,"battery=91.2"),10U,frame,error),"telemetry frame encoding failed"); require(station.ingest_frame(frame,error)==GroundIngestStatus::Accepted,"telemetry rejected");
    require(encode_frame(make_packet(GroundPacketType::Science,1U,"APXS|1850cps|Fe=0.22"),11U,frame,error),"science frame encoding failed"); require(station.ingest_frame(frame,error)==GroundIngestStatus::Accepted,"science rejected");
    require(encode_frame(make_packet(GroundPacketType::Event,1U,"THERMAL_WARNING"),12U,frame,error),"event frame encoding failed"); require(station.ingest_frame(frame,error)==GroundIngestStatus::Accepted,"event rejected");
    require(station.pending_packets()==3U,"three downlink packets should be queued");
}
void test_order_and_crc(){
    GroundStation station(GroundStationConfiguration{"GS-TRISHULA-01",8U,true,true}); std::string error; SpaceLinkFrame frame{};
    require(encode_frame(make_packet(GroundPacketType::Telemetry,5U,"temp=275"),20U,frame,error),"first frame encoding failed"); require(station.ingest_frame(frame,error)==GroundIngestStatus::Accepted,"first rejected");
    require(encode_frame(make_packet(GroundPacketType::Telemetry,5U,"temp=275"),21U,frame,error),"duplicate frame encoding failed"); require(station.ingest_frame(frame,error)==GroundIngestStatus::Duplicate,"duplicate not rejected");
    require(encode_frame(make_packet(GroundPacketType::Telemetry,4U,"temp=274"),22U,frame,error),"old frame encoding failed"); require(station.ingest_frame(frame,error)==GroundIngestStatus::OutOfOrder,"out-of-order not rejected");
    require(station.stats().duplicate_packets==1U,"duplicate counter incorrect"); require(station.stats().out_of_order_packets==1U,"out-of-order counter incorrect");
    require(encode_frame(make_packet(GroundPacketType::Telemetry,9U,"voltage=28.4"),30U,frame,error),"CRC frame encoding failed"); require(validate_frame(frame),"fresh frame invalid"); frame.packet_bytes.back()^=0x01U;
    require(!validate_frame(frame),"corrupted frame passed CRC"); require(station.ingest_frame(frame,error)==GroundIngestStatus::InvalidFrame,"corrupted frame accepted"); require(station.stats().crc_failures==1U,"CRC counter incorrect");
}
}
int main(){try{test_round_trip();test_ingest();test_order_and_crc();std::cout<<"TRISHULA V0.9.49 - Ground Station Core + Packet/Frame Processing\n===============================================================\n  packet encode/decode round trip       : PASS\n  Vikram telemetry/science/event ingest : PASS\n  duplicate detection                   : PASS\n  out-of-order detection               : PASS\n  frame CRC validation                 : PASS\n  ground station receive queue         : PASS\n  campaign                              : PASS\n\nV0.9.49 ground station packet campaign PASSED.\n";return 0;}catch(const std::exception& e){std::cerr<<"V0.9.49 campaign failed: "<<e.what()<<'\n';return 1;}}
