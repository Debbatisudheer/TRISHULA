#include "trishula/sensors/sensors.h"

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>

namespace trishula {

namespace {

void validate_non_negative(double value, const char* name) {
    if (value < 0.0) {
        throw std::invalid_argument(std::string(name) + " cannot be negative");
    }
}

} // namespace

SensorSuite::SensorSuite(SensorConfiguration configuration)
    : configuration_(configuration), random_engine_(configuration.random_seed) {
    validate_non_negative(
        configuration_.imu.accelerometer_noise_std_m_per_s2,
        "Accelerometer noise standard deviation");
    validate_non_negative(
        configuration_.imu.gyroscope_noise_std_rad_s,
        "Gyroscope noise standard deviation");
    validate_non_negative(
        configuration_.altimeter.noise_std_meters,
        "Altimeter noise standard deviation");
    validate_non_negative(
        configuration_.velocity.noise_std_m_per_s,
        "Velocity sensor noise standard deviation");
}

SensorData SensorSuite::measure(
    const TrueState& true_state,
    const Vector3& gravitational_acceleration_inertial_m_per_s2,
    double reference_body_radius_meters) {
    const Vector3 specific_force_inertial =
        true_state.acceleration_m_per_s2 - gravitational_acceleration_inertial_m_per_s2;
    const Vector3 specific_force_body =
        true_state.attitude_body_to_inertial.inverse().rotate(specific_force_inertial);

    const Vector3 measured_acceleration =
        specific_force_body + configuration_.imu.accelerometer_bias_m_per_s2 +
        gaussian_vector(configuration_.imu.accelerometer_noise_std_m_per_s2);

    const Vector3 measured_gyro =
        true_state.angular_velocity_rad_s + configuration_.imu.gyroscope_bias_rad_s +
        gaussian_vector(configuration_.imu.gyroscope_noise_std_rad_s);

    if (reference_body_radius_meters <= 0.0) {
        throw std::invalid_argument("Reference body radius must be positive");
    }

    // V0.4 uses a spherical primary-body altitude model. Surface relief and atmosphere
    // are intentionally out of scope at this stage.
    const double true_altitude = std::max(0.0, true_state.position_meters.magnitude() - reference_body_radius_meters);
    const double altitude_measurement =
        std::max(0.0, true_altitude + gaussian(configuration_.altimeter.noise_std_meters));

    const Vector3 measured_velocity =
        true_state.velocity_m_per_s + gaussian_vector(configuration_.velocity.noise_std_m_per_s);

    return {
        true_state.time_seconds,
        {measured_acceleration, measured_gyro},
        altitude_measurement,
        measured_velocity};
}

const SensorConfiguration& SensorSuite::configuration() const noexcept {
    return configuration_;
}

double SensorSuite::gaussian(double standard_deviation) {
    if (standard_deviation == 0.0) {
        return 0.0;
    }
    std::normal_distribution<double> distribution{0.0, standard_deviation};
    return distribution(random_engine_);
}

Vector3 SensorSuite::gaussian_vector(double standard_deviation) {
    return {gaussian(standard_deviation),
            gaussian(standard_deviation),
            gaussian(standard_deviation)};
}

} // namespace trishula
