#include "trishula/mission/physical_mission_ground_uplink.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

namespace trishula {
namespace {

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        WSACleanup();
#endif
    }
};

void close_socket(SocketHandle socket) noexcept {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool send_all(SocketHandle socket, const char* data, std::size_t length) {
    std::size_t sent = 0U;
    while (sent < length) {
#ifdef _WIN32
        const int n = send(socket, data + sent, static_cast<int>(length - sent), 0);
#else
        const auto n = send(socket, data + sent, length - sent, 0);
#endif
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool parse_http_url(const std::string& url, std::string& host, std::string& port, std::string& path, std::string& error) {
    constexpr std::string_view scheme = "http://";
    if (url.rfind(scheme, 0U) != 0U) {
        error = "only http:// URLs are supported";
        return false;
    }
    const std::size_t authority_begin = scheme.size();
    const std::size_t path_begin = url.find('/', authority_begin);
    const std::string authority = path_begin == std::string::npos
        ? url.substr(authority_begin)
        : url.substr(authority_begin, path_begin - authority_begin);
    path = path_begin == std::string::npos ? "/" : url.substr(path_begin);
    if (authority.empty()) {
        error = "HTTP host is empty";
        return false;
    }
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(':') == colon) {
        host = authority.substr(0U, colon);
        port = authority.substr(colon + 1U);
    } else {
        host = authority;
        port = "80";
    }
    if (host.empty() || port.empty()) {
        error = "HTTP host or port is empty";
        return false;
    }
    return true;
}

std::string json_escape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8U);
    for (const unsigned char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20U) {
                std::ostringstream code;
                code << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                out += code.str();
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    return out;
}

std::string number(double value) {
    if (!std::isfinite(value)) throw std::runtime_error("non-finite physical telemetry value");
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

} // namespace

PhysicalMissionGroundUplink::PhysicalMissionGroundUplink(PhysicalMissionGroundUplinkConfiguration configuration)
    : configuration_(std::move(configuration)), controller_(configuration_.mission) {
    if (configuration_.ticks == 0U) throw std::invalid_argument("ticks must be positive");
    reset();
}

void PhysicalMissionGroundUplink::reset() {
    controller_.reset();
    stats_ = {};
    record_sequence_ = 0U;
    record_id_sequence_ = 0U;
    mission_epoch_ns_ = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string PhysicalMissionGroundUplink::phase_name() const {
    switch (controller_.snapshot().phase) {
    case MissionPhase::Launch: return "Launch";
    case MissionPhase::EarthOrbit: return "Earth Orbit";
    case MissionPhase::TransLunarInjection: return "Trans-Lunar Injection";
    case MissionPhase::LunarCruise: return "Cruise to Moon";
    case MissionPhase::LunarOrbit: return "Lunar Orbit";
    case MissionPhase::Descent: return "Descent";
    case MissionPhase::Landing: return "Landing";
    case MissionPhase::RoverDeployment: return "Rover Deployment";
    case MissionPhase::SurfaceOperations: return "Surface Operations";
    case MissionPhase::Complete: return "Complete";
    case MissionPhase::Fault: return "Fault";
    }
    return "Unknown";
}

std::vector<PhysicalMissionGroundUplink::Metric> PhysicalMissionGroundUplink::metrics() const {
    const auto& s = controller_.mission().snapshot();
    const auto& p = controller_.snapshot();
    return {
        {"position_x_m", "navigation", "m", s.position_x_m},
        {"position_y_m", "navigation", "m", s.position_y_m},
        {"position_z_m", "navigation", "m", s.position_z_m},
        {"velocity_x_m_per_s", "navigation", "m/s", s.velocity_x_m_per_s},
        {"velocity_y_m_per_s", "navigation", "m/s", s.velocity_y_m_per_s},
        {"velocity_z_m_per_s", "navigation", "m/s", s.velocity_z_m_per_s},
        {"altitude_m", "navigation", "m", s.altitude_m},
        {"speed_m_per_s", "navigation", "m/s", s.speed_m_per_s},
        {"distance_to_moon_m", "navigation", "m", s.distance_to_moon_m},
        {"moon_x_m", "ephemeris", "m", s.moon_x_m},
        {"moon_y_m", "ephemeris", "m", s.moon_y_m},
        {"mission_time_seconds", "mission", "s", p.mission_time_seconds},
        {"phase_elapsed_seconds", "mission", "s", p.phase_elapsed_seconds},
        {"phase_progress", "mission", "ratio", p.phase_progress},
    };
}

std::string PhysicalMissionGroundUplink::make_record_json(const Metric& metric, const std::uint64_t record_sequence) {
    const auto& p = controller_.snapshot();
    const std::string record_id = "PHYS-V0941-" + std::to_string(++record_id_sequence_);
    std::ostringstream body;
    body << "{\"envelope\":{";
    body << "\"schema_version\":1,";
    body << "\"kind\":\"telemetry\",";
    body << "\"record_id\":\"" << json_escape(record_id) << "\",";
    body << "\"mission_id\":\"" << json_escape(configuration_.mission_id) << "\",";
    body << "\"source_node\":\"" << json_escape(configuration_.source_node) << "\",";
    body << "\"origin_node\":\"" << json_escape(configuration_.origin_node) << "\",";
    body << "\"destination_node\":\"" << json_escape(configuration_.destination_node) << "\",";
    body << "\"priority\":\"normal\",";
    body << "\"application_id\":" << configuration_.application_id << ",";
    body << "\"mission_timestamp_ns\":" << (mission_epoch_ns_ + static_cast<std::uint64_t>(p.mission_time_seconds * 1.0e9)) << ",";
    body << "\"sequence_number\":" << record_sequence << ",";
    body << "\"quality\":1.0,";
    body << "\"correlation_id\":\"PHYS-V0941-TICK-" << p.telemetry_sequence << "\",";
    body << "\"payload_schema\":\"trishula.physical.telemetry.v0.9.41\"},";
    body << "\"fields\":{";
    body << "\"metric\":\"" << metric.name << "\",";
    body << "\"value\":\"" << number(metric.value) << "\",";
    body << "\"unit\":\"" << metric.unit << "\",";
    body << "\"subsystem\":\"" << metric.subsystem << "\",";
    body << "\"quality\":\"1\",";
    body << "\"mission_phase\":\"" << json_escape(phase_name()) << "\",";
    body << "\"phase_elapsed_seconds\":\"" << number(p.phase_elapsed_seconds) << "\",";
    body << "\"phase_progress\":\"" << number(p.phase_progress) << "\",";
    body << "\"physical_telemetry\":\"true\"}}";
    return body.str();
}

bool PhysicalMissionGroundUplink::post_json(const std::string& body, std::string& error) const {
    error.clear();
    std::string host, port, path;
    if (!parse_http_url(configuration_.api_url, host, port, path, error)) return false;

    SocketRuntime runtime;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &results) != 0 || results == nullptr) {
        error = "unable to resolve ground API host";
        return false;
    }

    SocketHandle socket = kInvalidSocket;
    for (addrinfo* current = results; current != nullptr; current = current->ai_next) {
        socket = static_cast<SocketHandle>(::socket(current->ai_family, current->ai_socktype, current->ai_protocol));
        if (socket == kInvalidSocket) continue;
        if (::connect(socket, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) break;
        close_socket(socket);
        socket = kInvalidSocket;
    }
    freeaddrinfo(results);
    if (socket == kInvalidSocket) {
        error = "unable to connect to ground API";
        return false;
    }

    const std::string request =
        "POST " + path + " HTTP/1.1\r\n"
        "Host: " + host + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n\r\n" + body;
    if (!send_all(socket, request.data(), request.size())) {
        close_socket(socket);
        error = "failed to send ground API request";
        return false;
    }

    std::string response;
    char buffer[2048];
    for (;;) {
#ifdef _WIN32
        const int n = recv(socket, buffer, sizeof(buffer), 0);
#else
        const auto n = recv(socket, buffer, sizeof(buffer), 0);
#endif
        if (n <= 0) break;
        response.append(buffer, static_cast<std::size_t>(n));
        if (response.size() > 65536U) break;
    }
    close_socket(socket);

    const std::size_t line_end = response.find("\r\n");
    if (line_end == std::string::npos) {
        error = "ground API returned an invalid HTTP response";
        return false;
    }
    std::istringstream status_line(response.substr(0U, line_end));
    std::string http_version;
    int status = 0;
    status_line >> http_version >> status;
    if (status < 200 || status >= 300) {
        error = "ground API returned HTTP " + std::to_string(status);
        return false;
    }
    return true;
}

void PhysicalMissionGroundUplink::publish_metric(const Metric& metric) {
    ++stats_.records_attempted;
    const std::uint64_t sequence = ++record_sequence_;
    std::string error;
    if (!post_json(make_record_json(metric, sequence), error)) {
        ++stats_.records_rejected;
        throw std::runtime_error("physical ground uplink failed: " + error);
    }
    ++stats_.records_accepted;
}

void PhysicalMissionGroundUplink::step() {
    const auto before = controller_.snapshot().phase_transition_count;
    controller_.step();
    ++stats_.simulation_ticks;
    for (const auto& metric : metrics()) publish_metric(metric);
    stats_.phase_transitions = controller_.snapshot().phase_transition_count;
    (void)before;
}

void PhysicalMissionGroundUplink::run() {
    for (std::size_t tick = 0U; tick < configuration_.ticks; ++tick) {
        step();
        if (controller_.snapshot().complete) break;
        if (configuration_.interval_ms > 0U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(configuration_.interval_ms));
        }
    }
}

const PhysicalMissionPhaseController& PhysicalMissionGroundUplink::controller() const noexcept { return controller_; }
const PhysicalMissionGroundUplinkStats& PhysicalMissionGroundUplink::stats() const noexcept { return stats_; }

} // namespace trishula
