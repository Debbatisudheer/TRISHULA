#pragma once

#include <array>

#include "trishula/math/quaternion.h"
#include "trishula/sensors/sensors.h"

namespace trishula {

struct EstimatedState {
    double time_seconds{0.0};
    Vector3 position_meters{};
    Vector3 velocity_m_per_s{};
    Vector3 acceleration_m_per_s2{};
    Quaternion attitude_body_to_inertial{};
    Vector3 angular_velocity_rad_s{};
    double altitude_meters{0.0};
};

struct EstimatorConfiguration {
    // V0.9.5: covariance-based linearized state estimator for position/velocity.
    // The public gain fields are retained for source compatibility but are no
    // longer the primary fusion mechanism.
    double velocity_measurement_gain{0.35};
    double altitude_measurement_gain{0.25};
    double max_position_correction_meters{500.0};
    double max_velocity_correction_m_per_s{2.0};

    bool enable_covariance_filter{true};
    double initial_position_std_meters{100.0};
    double initial_velocity_std_m_per_s{1.0};
    double process_acceleration_noise_std_m_per_s2{0.01};
    double altitude_measurement_noise_std_meters{0.5};
    double velocity_measurement_noise_std_m_per_s{0.005};
};

class StateEstimator {
public:
    StateEstimator(
        Vector3 initial_position_meters,
        Vector3 initial_velocity_m_per_s,
        Quaternion initial_attitude_body_to_inertial = {},
        EstimatorConfiguration configuration = {});

    [[nodiscard]] EstimatedState update(
        const SensorData& measurements,
        const Vector3& gravitational_acceleration_inertial_m_per_s2,
        double reference_body_radius_meters,
        double time_step_seconds);

    [[nodiscard]] const EstimatedState& state() const noexcept;
    [[nodiscard]] const EstimatorConfiguration& configuration() const noexcept;
    [[nodiscard]] double position_uncertainty_1sigma_meters() const noexcept;
    [[nodiscard]] double velocity_uncertainty_1sigma_m_per_s() const noexcept;
    void reset(const EstimatedState& state);

private:
    using Covariance = std::array<std::array<double, 6>, 6>;

    static Covariance identity_covariance(double diagonal);
    static void symmetrize(Covariance& matrix);
    static double covariance_trace_position(const Covariance& matrix);
    static double covariance_trace_velocity(const Covariance& matrix);

    void initialize_covariance();
    void predict_covariance(double dt_seconds);
    void update_velocity_measurement(const Vector3& measurement_m_per_s);
    void update_altitude_measurement(
        double measurement_altitude_m,
        const Vector3& position_meters,
        double reference_body_radius_meters);

    EstimatedState state_{};
    EstimatorConfiguration configuration_{};
    Covariance covariance_{};
    bool initialized_{false};
};

} // namespace trishula
