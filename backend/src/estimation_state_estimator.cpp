#include "trishula/estimation/state_estimator.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace trishula {
namespace {

 double component(const Vector3& v, std::size_t i) {
    return i == 0 ? v.x : (i == 1 ? v.y : v.z);
}

void add_component(Vector3& v, std::size_t i, double value) {
    if (i == 0) v.x += value;
    else if (i == 1) v.y += value;
    else v.z += value;
}

Quaternion integrate_gyro(
    const Quaternion& current,
    const Vector3& angular_velocity_body_rad_s,
    double dt_seconds) {
    const Quaternion omega{0.0,
                           angular_velocity_body_rad_s.x,
                           angular_velocity_body_rad_s.y,
                           angular_velocity_body_rad_s.z};
    const Quaternion derivative = current * omega * 0.5;
    return (current + derivative * dt_seconds).normalized();
}

void validate_nonnegative(double value, const char* name) {
    if (value < 0.0) {
        throw std::invalid_argument(std::string(name) + " must be non-negative");
    }
}

} // namespace

StateEstimator::StateEstimator(
    Vector3 initial_position_meters,
    Vector3 initial_velocity_m_per_s,
    Quaternion initial_attitude_body_to_inertial,
    EstimatorConfiguration configuration)
    : configuration_(configuration) {
    if (configuration_.max_position_correction_meters <= 0.0) {
        throw std::invalid_argument("Maximum position correction must be positive");
    }
    validate_nonnegative(configuration_.initial_position_std_meters, "Initial position std");
    validate_nonnegative(configuration_.initial_velocity_std_m_per_s, "Initial velocity std");
    validate_nonnegative(configuration_.process_acceleration_noise_std_m_per_s2,
                         "Process acceleration noise std");
    if (configuration_.altitude_measurement_noise_std_meters <= 0.0) {
        throw std::invalid_argument("Altitude measurement noise std must be positive");
    }
    if (configuration_.velocity_measurement_noise_std_m_per_s <= 0.0) {
        throw std::invalid_argument("Velocity measurement noise std must be positive");
    }

    state_.position_meters = initial_position_meters;
    state_.velocity_m_per_s = initial_velocity_m_per_s;
    state_.attitude_body_to_inertial = initial_attitude_body_to_inertial.normalized();
    initialize_covariance();
    initialized_ = true;
}

StateEstimator::Covariance StateEstimator::identity_covariance(double diagonal) {
    Covariance result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i][i] = diagonal;
    }
    return result;
}

void StateEstimator::symmetrize(Covariance& matrix) {
    for (std::size_t i = 0; i < matrix.size(); ++i) {
        for (std::size_t j = i + 1; j < matrix.size(); ++j) {
            const double value = 0.5 * (matrix[i][j] + matrix[j][i]);
            matrix[i][j] = value;
            matrix[j][i] = value;
        }
        matrix[i][i] = std::max(0.0, matrix[i][i]);
    }
}

double StateEstimator::covariance_trace_position(const Covariance& matrix) {
    return std::max(0.0, matrix[0][0] + matrix[1][1] + matrix[2][2]);
}

double StateEstimator::covariance_trace_velocity(const Covariance& matrix) {
    return std::max(0.0, matrix[3][3] + matrix[4][4] + matrix[5][5]);
}

void StateEstimator::initialize_covariance() {
    covariance_ = {};
    const double position_variance =
        configuration_.initial_position_std_meters * configuration_.initial_position_std_meters;
    const double velocity_variance =
        configuration_.initial_velocity_std_m_per_s * configuration_.initial_velocity_std_m_per_s;
    for (std::size_t i = 0; i < 3; ++i) {
        covariance_[i][i] = position_variance;
        covariance_[i + 3][i + 3] = velocity_variance;
    }
}

void StateEstimator::predict_covariance(double dt_seconds) {
    // Full F P F^T propagation for the six-state [position, velocity] model.
    // F = [I dtI; 0 I]. White acceleration drives the process covariance.
    std::array<std::array<double, 6>, 6> f{};
    for (std::size_t i = 0; i < 6; ++i) {
        f[i][i] = 1.0;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        f[axis][axis + 3] = dt_seconds;
    }

    const double dt2 = dt_seconds * dt_seconds;
    const double dt3 = dt2 * dt_seconds;
    const double dt4 = dt2 * dt2;
    const double sigma_a2 =
        configuration_.process_acceleration_noise_std_m_per_s2 *
        configuration_.process_acceleration_noise_std_m_per_s2;

    std::array<std::array<double, 6>, 6> q{};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        q[axis][axis] = 0.25 * dt4 * sigma_a2;
        q[axis][axis + 3] = 0.5 * dt3 * sigma_a2;
        q[axis + 3][axis] = q[axis][axis + 3];
        q[axis + 3][axis + 3] = dt2 * sigma_a2;
    }

    Covariance predicted{};
    for (std::size_t i = 0; i < 6; ++i) {
        for (std::size_t j = 0; j < 6; ++j) {
            double value = q[i][j];
            for (std::size_t a = 0; a < 6; ++a) {
                for (std::size_t b = 0; b < 6; ++b) {
                    value += f[i][a] * covariance_[a][b] * f[j][b];
                }
            }
            predicted[i][j] = value;
        }
    }
    covariance_ = predicted;
    symmetrize(covariance_);
}

void StateEstimator::update_velocity_measurement(const Vector3& measurement_m_per_s) {
    const double r = configuration_.velocity_measurement_noise_std_m_per_s *
                     configuration_.velocity_measurement_noise_std_m_per_s;

    for (std::size_t axis = 0; axis < 3; ++axis) {
        const std::size_t measurement_index = axis + 3;
        const double residual =
            component(measurement_m_per_s, axis) - component(state_.velocity_m_per_s, axis);
        const double innovation_variance = covariance_[measurement_index][measurement_index] + r;
        if (innovation_variance <= 0.0) {
            continue;
        }

        std::array<double, 6> gain{};
        for (std::size_t row = 0; row < 6; ++row) {
            gain[row] = covariance_[row][measurement_index] / innovation_variance;
        }

        for (std::size_t row = 0; row < 3; ++row) {
            add_component(state_.position_meters, row, gain[row] * residual);
            add_component(state_.velocity_m_per_s, row, gain[row + 3] * residual);
        }

        // Joseph form for H = e_measurement_index^T.
        Covariance updated = covariance_;
        for (std::size_t row = 0; row < 6; ++row) {
            for (std::size_t col = 0; col < 6; ++col) {
                updated[row][col] = covariance_[row][col]
                    - gain[row] * covariance_[measurement_index][col]
                    - covariance_[row][measurement_index] * gain[col]
                    + gain[row] * innovation_variance * gain[col];
            }
        }
        covariance_ = updated;
        symmetrize(covariance_);
    }
}

void StateEstimator::update_altitude_measurement(
    double measurement_altitude_m,
    const Vector3& position_meters,
    double reference_body_radius_meters) {
    const double radius = position_meters.magnitude();
    if (radius <= 1.0e-9) {
        return;
    }

    const double radial_residual =
        measurement_altitude_m - (radius - reference_body_radius_meters);
    const std::array<double, 6> h{
        position_meters.x / radius,
        position_meters.y / radius,
        position_meters.z / radius,
        0.0, 0.0, 0.0};

    const double r = configuration_.altitude_measurement_noise_std_meters *
                     configuration_.altitude_measurement_noise_std_meters;
    double innovation_variance = r;
    for (std::size_t i = 0; i < 6; ++i) {
        for (std::size_t j = 0; j < 6; ++j) {
            innovation_variance += h[i] * covariance_[i][j] * h[j];
        }
    }
    if (innovation_variance <= 0.0) {
        return;
    }

    std::array<double, 6> gain{};
    for (std::size_t row = 0; row < 6; ++row) {
        for (std::size_t col = 0; col < 6; ++col) {
            gain[row] += covariance_[row][col] * h[col];
        }
        gain[row] /= innovation_variance;
    }

    for (std::size_t row = 0; row < 3; ++row) {
        add_component(state_.position_meters, row, gain[row] * radial_residual);
        add_component(state_.velocity_m_per_s, row, gain[row + 3] * radial_residual);
    }

    Covariance updated = covariance_;
    for (std::size_t row = 0; row < 6; ++row) {
        for (std::size_t col = 0; col < 6; ++col) {
            updated[row][col] = covariance_[row][col]
                - gain[row] * (h[0] * covariance_[0][col] +
                               h[1] * covariance_[1][col] +
                               h[2] * covariance_[2][col])
                - (covariance_[row][0] * h[0] +
                   covariance_[row][1] * h[1] +
                   covariance_[row][2] * h[2]) * gain[col]
                + gain[row] * innovation_variance * gain[col];
        }
    }
    covariance_ = updated;
    symmetrize(covariance_);
}

EstimatedState StateEstimator::update(
    const SensorData& measurements,
    const Vector3& gravitational_acceleration_inertial_m_per_s2,
    double reference_body_radius_meters,
    double time_step_seconds) {
    if (!initialized_) {
        throw std::logic_error("State estimator is not initialized");
    }
    if (time_step_seconds <= 0.0) {
        throw std::invalid_argument("Estimator time step must be positive");
    }
    if (reference_body_radius_meters <= 0.0) {
        throw std::invalid_argument("Reference body radius must be positive");
    }

    const Vector3 specific_force_inertial =
        state_.attitude_body_to_inertial.rotate(measurements.imu.specific_force_body_m_per_s2);
    const Vector3 predicted_acceleration =
        specific_force_inertial + gravitational_acceleration_inertial_m_per_s2;

    state_.velocity_m_per_s += predicted_acceleration * time_step_seconds;
    state_.position_meters += state_.velocity_m_per_s * time_step_seconds;
    state_.acceleration_m_per_s2 = predicted_acceleration;

    if (configuration_.enable_covariance_filter) {
        predict_covariance(time_step_seconds);
        update_velocity_measurement(measurements.velocity_m_per_s);
        update_altitude_measurement(
            measurements.altitude_meters,
            state_.position_meters,
            reference_body_radius_meters);
    } else {
        state_.velocity_m_per_s +=
            (measurements.velocity_m_per_s - state_.velocity_m_per_s) *
            configuration_.velocity_measurement_gain;
        const double predicted_altitude =
            std::max(0.0, state_.position_meters.magnitude() - reference_body_radius_meters);
        const double altitude_residual = measurements.altitude_meters - predicted_altitude;
        const double radius = state_.position_meters.magnitude();
        if (radius > 0.0) {
            const Vector3 radial_direction = state_.position_meters / radius;
            const double correction = std::clamp(
                altitude_residual * configuration_.altitude_measurement_gain,
                -configuration_.max_position_correction_meters,
                configuration_.max_position_correction_meters);
            state_.position_meters += radial_direction * correction;
        }
    }

    state_.attitude_body_to_inertial = integrate_gyro(
        state_.attitude_body_to_inertial,
        measurements.imu.angular_velocity_body_rad_s,
        time_step_seconds);
    state_.angular_velocity_rad_s = measurements.imu.angular_velocity_body_rad_s;
    state_.time_seconds = measurements.timestamp_seconds;
    state_.altitude_meters = std::max(
        0.0, state_.position_meters.magnitude() - reference_body_radius_meters);

    return state_;
}

const EstimatedState& StateEstimator::state() const noexcept {
    return state_;
}

const EstimatorConfiguration& StateEstimator::configuration() const noexcept {
    return configuration_;
}

double StateEstimator::position_uncertainty_1sigma_meters() const noexcept {
    return std::sqrt(covariance_trace_position(covariance_));
}

double StateEstimator::velocity_uncertainty_1sigma_m_per_s() const noexcept {
    return std::sqrt(covariance_trace_velocity(covariance_));
}

void StateEstimator::reset(const EstimatedState& state) {
    state_ = state;
    state_.attitude_body_to_inertial = state_.attitude_body_to_inertial.normalized();
    initialize_covariance();
    initialized_ = true;
}

} // namespace trishula
