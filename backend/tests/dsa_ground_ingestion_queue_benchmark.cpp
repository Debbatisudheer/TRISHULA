#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "trishula/ground/command_ingestion.h"
#include "trishula/ground/event_ingestion.h"
#include "trishula/ground/telemetry_ingestion.h"

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t kOperations = 20000U;

trishula::GroundPacket telemetry_packet(std::uint32_t sequence) {
    trishula::GroundPacket packet{};
    packet.header.type = trishula::GroundPacketType::Telemetry;
    packet.header.source_node = "VIKRAM";
    packet.header.origin_node = "VIKRAM";
    packet.header.destination_node = "GS-TRISHULA-01";
    packet.header.application_id = 101U;
    packet.header.sequence_number = sequence;
    const std::string payload = "metric=temperature|value=273|unit=K|subsystem=thermal";
    packet.payload.assign(payload.begin(), payload.end());
    return packet;
}

template <typename Pipeline, typename PacketFactory, typename PopFn>
double benchmark(Pipeline& pipeline, PacketFactory make_packet, PopFn pop) {
    std::string error;
    const auto begin = Clock::now();
    for (std::uint32_t i = 1U; i <= kOperations; ++i) {
        if (pipeline.ingest(make_packet(i), error) != decltype(pipeline.ingest(make_packet(i), error))::Accepted) {
            std::cerr << "ingest failed: " << error << '\n';
            return -1.0;
        }
    }
    std::uint32_t drained = 0U;
    while (pop()) ++drained;
    const auto end = Clock::now();
    if (drained != kOperations) return -1.0;
    return std::chrono::duration<double, std::milli>(end - begin).count();
}
} // namespace

int main() {
    using namespace trishula;
    TelemetryIngestionPipeline telemetry(TelemetryIngestionConfiguration{kOperations, true, true});
    const double ms = benchmark(
        telemetry,
        telemetry_packet,
        [&telemetry]() { TelemetryRecord record{}; return telemetry.pop_record(record); });
    if (ms < 0.0) return 1;

    std::cout << "TRISHULA V0.9.106.1 Ground Ingestion Queue Benchmark\n"
              << "operations=" << kOperations << " enqueue+dequeue cycles\n"
              << "telemetry_elapsed_ms=" << std::fixed << std::setprecision(3) << ms << '\n'
              << "queue implementation=std::deque\n";
    return 0;
}
