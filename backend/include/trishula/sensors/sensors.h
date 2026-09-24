#pragma once

#include <cstdint>
#include <random>

#include "trishula/core/vector3.h"
#include "trishula/physics/state.h"

namespace trishula {

struct ImuConfiguration {
    double accelerometer_noise_std_m_per_s2{0.0};
    double gyroscope_noise_std_rad_s{0.0};
    Vector3 accelerometer_bias_m_per_s2{};
    Vector3 gyroscope_bias_rad_s{};
};

struct AltimeterConfiguration {
    double noise_std_meters{0.0};
};

struct VelocitySensorConfiguration {
    double noise_std_m_per_s{0.0};
};

struct SensorConfiguration {
    ImuConfiguration imu{};
    AltimeterConfiguration altimeter{};
    VelocitySensorConfiguration velocity{};
    std::uint64_t random_seed{1};
};

struct ImuMeasurement {
    Vector3 specific_force_body_m_per_s2{};
    Vector3 angular_velocity_body_rad_s{};
};

struct SensorData {
    double timestamp_seconds{0.0};
    ImuMeasurement imu{};
    double altitude_meters{0.0};
    Vector3 velocity_m_per_s{};
};

class SensorSuite {
public:
    SensorSuite(SensorConfiguration configuration = {});

    [[nodiscard]] SensorData measure(
        const TrueState& true_state,
        const Vector3& gravitational_acceleration_inertial_m_per_s2,
        double reference_body_radius_meters);

    [[nodiscard]] const SensorConfiguration& configuration() const noexcept;

private:
    [[nodiscard]] double gaussian(double standard_deviation);
    [[nodiscard]] Vector3 gaussian_vector(double standard_deviation);

    SensorConfiguration configuration_;
    std::mt19937_64 random_engine_;
};

} // namespace trishula
