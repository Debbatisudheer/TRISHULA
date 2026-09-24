#include "trishula/ground/command_execution.h"
#include "trishula/ground/vehicle_command_adapter.h"

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_tab(const std::string& line) {
    std::vector<std::string> out;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const auto end = line.find('\t', start);
        out.push_back(line.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return out;
}

std::string hex_encode(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const unsigned char ch : value) {
        out << std::setw(2) << static_cast<unsigned int>(ch);
    }
    return out.str();
}

bool hex_value(char c, unsigned char& out) {
    if (c >= '0' && c <= '9') { out = static_cast<unsigned char>(c - '0'); return true; }
    if (c >= 'a' && c <= 'f') { out = static_cast<unsigned char>(10 + c - 'a'); return true; }
    if (c >= 'A' && c <= 'F') { out = static_cast<unsigned char>(10 + c - 'A'); return true; }
    return false;
}

bool hex_decode(const std::string& hex, std::string& out) {
    if (hex.size() % 2U != 0U) return false;
    out.clear();
    out.reserve(hex.size() / 2U);
    for (std::size_t i = 0; i < hex.size(); i += 2U) {
        unsigned char hi = 0U;
        unsigned char lo = 0U;
        if (!hex_value(hex[i], hi) || !hex_value(hex[i + 1U], lo)) return false;
        out.push_back(static_cast<char>((hi << 4U) | lo));
    }
    return true;
}

std::string adapter_status_name(const trishula::VehicleAdapterStatus status) {
    switch (status) {
    case trishula::VehicleAdapterStatus::Applied: return "APPLIED";
    case trishula::VehicleAdapterStatus::UnsupportedTarget: return "UNSUPPORTED_TARGET";
    case trishula::VehicleAdapterStatus::UnsupportedOpcode: return "UNSUPPORTED_OPCODE";
    case trishula::VehicleAdapterStatus::PreconditionsFailed: return "PRECONDITIONS_FAILED";
    case trishula::VehicleAdapterStatus::InvalidParameters: return "INVALID_PARAMETERS";
    case trishula::VehicleAdapterStatus::PhysicalFault: return "PHYSICAL_FAULT";
    }
    return "UNKNOWN";
}

std::string normalize_adapter_target(const std::string& target) {
    if (target == "ROVER-01" || target == "ROVER") return "ROVER";
    if (target == "VIKRAM") return "VIKRAM";
    if (target == "LANDER") return "LANDER";
    return target;
}

void write_error(const std::string& message) {
    std::cout << "ERROR\t" << hex_encode(message) << "\n" << std::flush;
}

} // namespace

int main() {
    trishula::GroundCommandExecutionEngine logical_engine;
    trishula::VehicleCommandAdapter physical_adapter;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        const auto fields = split_tab(line);
        if (fields.size() != 8U) {
            write_error("bridge request requires 8 tab-separated fields");
            continue;
        }

        std::uint64_t mission_timestamp_ns = 0U;
        std::uint32_t sequence_number = 0U;
        unsigned long application_id = 0UL;
        try {
            // Protocol: command_id, target, opcode, params_hex, mission_id, sequence, application_id, mission_timestamp_ns.
            mission_timestamp_ns = std::stoull(fields[7]);
            sequence_number = static_cast<std::uint32_t>(std::stoul(fields[5]));
            application_id = std::stoul(fields[6]);
        } catch (const std::exception&) {
            write_error("invalid numeric bridge fields");
            continue;
        }

        std::string parameters;
        if (!hex_decode(fields[3], parameters)) {
            write_error("invalid hex parameters");
            continue;
        }

        trishula::GroundCommandRecord command{};
        command.command_id = fields[0];
        command.source_node = "GROUND-OPS-01";
        command.destination_node = fields[1];
        command.target = normalize_adapter_target(fields[1]);
        command.opcode = fields[2];
        command.parameters = parameters;
        command.application_id = static_cast<std::uint16_t>(application_id);
        command.mission_timestamp_ns = mission_timestamp_ns;
        command.sequence_number = sequence_number;

        const auto logical = logical_engine.execute(command);
        const auto physical = physical_adapter.apply(command, logical);

        const auto& snapshot = physical.snapshot;
        std::cout
            << "RESULT\t"
            << adapter_status_name(physical.status) << '\t'
            << hex_encode(physical.message) << '\t'
            << snapshot.mode << '\t'
            << std::setprecision(17) << snapshot.x_m << '\t'
            << snapshot.y_m << '\t'
            << snapshot.heading_rad << '\t'
            << snapshot.speed_m_s << '\t'
            << snapshot.battery_soc << '\t'
            << (snapshot.deployed ? "1" : "0") << '\t'
            << (snapshot.mast_ready ? "1" : "0") << '\t'
            << (snapshot.localization_valid ? "1" : "0") << '\t'
            << (snapshot.hazard_detected ? "1" : "0") << '\t'
            << (snapshot.drive_system_healthy ? "1" : "0") << '\t'
            << snapshot.physical_steps << '\t'
            << hex_encode(snapshot.last_command_id) << '\t'
            << hex_encode(snapshot.last_opcode) << '\n'
            << std::flush;
    }

    return 0;
}
