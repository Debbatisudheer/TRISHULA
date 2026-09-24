#include <cmath>
#include <iomanip>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "trishula/core/simulation_engine.h"
#include "trishula/estimation/state_estimator.h"
#include "trishula/sensors/sensors.h"
#include "trishula/maneuver/orbital_maneuver.h"
#include "trishula/maneuver/recovery_maneuver_executor.h"
#include "trishula/maneuver/finite_burn_predictor.h"
#include "trishula/vehicle/spacecraft.h"
#include "trishula/physics/perturbations.h"
#include "trishula/navigation/navigation.h"

namespace {
trishula::EstimatedState as_estimated(const trishula::TrueState& state) {
    trishula::EstimatedState estimated{};
    estimated.time_seconds = state.time_seconds;
    estimated.position_meters = state.position_meters;
    estimated.velocity_m_per_s = state.velocity_m_per_s;
    estimated.acceleration_m_per_s2 = state.acceleration_m_per_s2;
    estimated.attitude_body_to_inertial = state.attitude_body_to_inertial;
    estimated.angular_velocity_rad_s = state.angular_velocity_rad_s;
    return estimated;
}

constexpr double kMuEarth = 3.986004418e14;
constexpr double kEarthRadius = 6.371e6;
constexpr double kInitialMassKg = 1000.0;
constexpr double kDryMassKg = 800.0;
constexpr double kNominalThrustN = 20000.0;
constexpr double kEngineIspS = 300.0;
constexpr double kRcsForceN = 20.0;
constexpr double kRcsIspS = 120.0;
constexpr double kStepS = 0.1;

struct CaseConfig {
    std::string name;
    double initial_altitude_m;
    double target_altitude_m;
    double actual_thrust_scale{1.0};
    double first_burn_duration_scale{1.0};
    double second_burn_duration_scale{1.0};
    double initial_tangential_velocity_offset_m_per_s{0.0};
    bool noisy_sensors{true};
    double actual_isp_scale{1.0};
    double initial_mass_scale{1.0};
    double sensor_noise_scale{1.0};
    double initial_position_std_scale{1.0};
    double initial_velocity_std_scale{1.0};
    double process_noise_scale{1.0};
    trishula::PerturbationConfiguration perturbation_configuration{};
};

struct CaseResult {
    CaseConfig config;
    trishula::RecoveryManeuverResult execution;
    double final_altitude_error_m{0.0};
    double final_speed_error_m_per_s{0.0};
    double final_semi_major_axis_error_m{0.0};
    double final_radial_velocity_m_per_s{0.0};
    double final_position_estimation_error_m{0.0};
    double final_velocity_estimation_error_m_per_s{0.0};
};

CaseResult run_case(const CaseConfig& cfg) {
    const double initial_radius = kEarthRadius + cfg.initial_altitude_m;
    const double target_radius = kEarthRadius + cfg.target_altitude_m;
    const double initial_speed = std::sqrt(kMuEarth / initial_radius) +
                                 cfg.initial_tangential_velocity_offset_m_per_s;

    const double actual_initial_mass = kInitialMassKg * cfg.initial_mass_scale;

    trishula::TrueState initial_state{};
    initial_state.position_meters = {initial_radius, 0.0, 0.0};
    initial_state.velocity_m_per_s = {0.0, initial_speed, 0.0};
    initial_state.attitude_body_to_inertial = {};
    initial_state.mass_kg = actual_initial_mass;

    const trishula::Spacecraft spacecraft(initial_state, {100.0, 100.0, 100.0});
    const trishula::CelestialBody earth(kMuEarth, kEarthRadius);
    const trishula::MainEngine engine(
        kNominalThrustN * cfg.actual_thrust_scale, kEngineIspS * cfg.actual_isp_scale, kDryMassKg);
    const trishula::RcsModule rcs(kRcsForceN, kRcsIspS, kDryMassKg);
    trishula::SensorConfiguration sensor_configuration{};
    sensor_configuration.imu.accelerometer_noise_std_m_per_s2 = cfg.noisy_sensors ? 0.0005 * cfg.sensor_noise_scale : 0.0;
    sensor_configuration.imu.gyroscope_noise_std_rad_s = cfg.noisy_sensors ? 2.0e-6 * cfg.sensor_noise_scale : 0.0;
    sensor_configuration.altimeter.noise_std_meters = cfg.noisy_sensors ? 0.5 * cfg.sensor_noise_scale : 0.0;
    sensor_configuration.velocity.noise_std_m_per_s = cfg.noisy_sensors ? 0.005 * cfg.sensor_noise_scale : 0.0;
    sensor_configuration.random_seed = 20260915;
    trishula::SimulationEngine simulation(earth, spacecraft, kStepS, engine, rcs, trishula::SensorSuite(sensor_configuration), cfg.perturbation_configuration);

    const trishula::HohmannTransferPlanner planner;
    auto plan = planner.plan(
        initial_radius, target_radius, kMuEarth, kInitialMassKg, kDryMassKg,
        kNominalThrustN, kEngineIspS);
    plan.first_burn_duration_seconds *= cfg.first_burn_duration_scale;
    plan.second_burn_duration_seconds *= cfg.second_burn_duration_scale;

    trishula::EstimatorConfiguration estimator_configuration{};
    estimator_configuration.enable_covariance_filter = true;
    estimator_configuration.initial_position_std_meters = 100.0 * cfg.initial_position_std_scale;
    estimator_configuration.initial_velocity_std_m_per_s = 1.0 * cfg.initial_velocity_std_scale;
    estimator_configuration.process_acceleration_noise_std_m_per_s2 = 0.05 * cfg.process_noise_scale;
    estimator_configuration.altitude_measurement_noise_std_meters = 0.5 * cfg.sensor_noise_scale;
    estimator_configuration.velocity_measurement_noise_std_m_per_s = 0.005 * cfg.sensor_noise_scale;
    estimator_configuration.max_position_correction_meters = 500.0;
    const trishula::RecoveryManeuverExecutor executor(kStepS, estimator_configuration);
    const auto execution = executor.execute(simulation, plan);
    const double target_speed = std::sqrt(kMuEarth / target_radius);

    return {cfg, execution,
            execution.final_orbit.altitude_meters - cfg.target_altitude_m,
            execution.final_orbit.speed_m_per_s - target_speed,
            execution.final_orbit.orbit.semi_major_axis_meters - target_radius,
            execution.final_orbit.radial_velocity_m_per_s,
            execution.final_position_estimation_error_m,
            execution.final_velocity_estimation_error_m_per_s};
}

double apoapsis_altitude_m(const trishula::NavigationState& nav, double body_radius_m) {
    if (!(nav.orbit.semi_major_axis_meters > 0.0) || !(nav.orbit.eccentricity >= 0.0)) return std::numeric_limits<double>::quiet_NaN();
    return nav.orbit.semi_major_axis_meters * (1.0 + nav.orbit.eccentricity) - body_radius_m;
}

double periapsis_altitude_m(const trishula::NavigationState& nav, double body_radius_m) {
    if (!(nav.orbit.semi_major_axis_meters > 0.0) || !(nav.orbit.eccentricity >= 0.0)) return std::numeric_limits<double>::quiet_NaN();
    return nav.orbit.semi_major_axis_meters * (1.0 - nav.orbit.eccentricity) - body_radius_m;
}

void print_stage_diagnostic(const std::string& name, const trishula::OrbitStageDiagnostic& d) {
    std::cout << "  " << name << "\n";
    std::cout << "    time truth/est         : " << d.truth_orbit.time_seconds << " / " << d.estimated_orbit.time_seconds << " s\n";
    std::cout << "    altitude truth/est     : " << d.truth_orbit.altitude_meters << " / " << d.estimated_orbit.altitude_meters << " m\n";
    std::cout << "    speed truth/est        : " << d.truth_orbit.speed_m_per_s << " / " << d.estimated_orbit.speed_m_per_s << " m/s\n";
    std::cout << "    semi-major a truth/est : " << d.truth_orbit.orbit.semi_major_axis_meters << " / " << d.estimated_orbit.orbit.semi_major_axis_meters << " m\n";
    std::cout << "    eccentricity truth/est : " << d.truth_orbit.orbit.eccentricity << " / " << d.estimated_orbit.orbit.eccentricity << "\n";
    std::cout << "    energy truth/est       : " << d.truth_orbit.orbit.specific_orbital_energy_j_per_kg << " / " << d.estimated_orbit.orbit.specific_orbital_energy_j_per_kg << " J/kg\n";
    std::cout << "    apoapsis truth/est     : " << apoapsis_altitude_m(d.truth_orbit, kEarthRadius) << " / " << apoapsis_altitude_m(d.estimated_orbit, kEarthRadius) << " m alt\n";
    std::cout << "    periapsis truth/est    : " << periapsis_altitude_m(d.truth_orbit, kEarthRadius) << " / " << periapsis_altitude_m(d.estimated_orbit, kEarthRadius) << " m alt\n";
    std::cout << "    position error         : " << d.position_estimation_error_m << " m\n";
    std::cout << "    velocity error         : " << d.velocity_estimation_error_m_per_s << " m/s\n";
}

void print_burn_diagnostic(const std::string& name, const trishula::BurnExecutionSummary& burn) {
    std::cout << "  " << name << "\n";
    std::cout << "    start/end time         : " << burn.start_time_seconds << " / " << burn.end_time_seconds << " s\n";
    std::cout << "    duration               : " << burn.duration_seconds << " s\n";
    std::cout << "    mass                   : " << burn.initial_mass_kg << " -> " << burn.final_mass_kg << " kg\n";
    std::cout << "    propellant             : " << burn.propellant_consumed_kg << " kg\n";
    std::cout << "    achieved propulsive dV : " << burn.achieved_propulsive_delta_v_m_per_s << " m/s\n";
}

void print_failure_reproduction(const std::string& title, const CaseResult& result) {
    std::cout << "\nFAILURE REPRODUCTION - " << title << "\n";
    std::cout << "==============================================================\n";
    std::cout << "  target altitude           : " << result.config.target_altitude_m << " m\n";
    std::cout << "  thrust scale              : " << result.config.actual_thrust_scale << "\n";
    std::cout << "  Isp scale                 : " << result.config.actual_isp_scale << "\n";
    std::cout << "  first duration scale      : " << result.config.first_burn_duration_scale << "\n";
    std::cout << "  second duration scale     : " << result.config.second_burn_duration_scale << "\n";
    std::cout << "  initial tangential dV     : " << result.config.initial_tangential_velocity_offset_m_per_s << " m/s\n";
    std::cout << "  initial mass scale        : " << result.config.initial_mass_scale << "\n";
    std::cout << "  sensor noise scale        : " << result.config.sensor_noise_scale << "\n";
    std::cout << "  initial position std scale: " << result.config.initial_position_std_scale << "\n";
    std::cout << "  initial velocity std scale: " << result.config.initial_velocity_std_scale << "\n";
    std::cout << "  process noise scale       : " << result.config.process_noise_scale << "\n\n";
    print_stage_diagnostic("AFTER FIRST PLANNED BURN", result.execution.after_first_planned_diagnostic);
    print_burn_diagnostic("FIRST PLANNED BURN", result.execution.first_planned_burn);
    print_stage_diagnostic("AFTER FIRST CORRECTION", result.execution.after_first_correction_diagnostic);
    print_burn_diagnostic("FIRST CORRECTION BURN", result.execution.first_correction_burn);
    print_stage_diagnostic("BEFORE SECOND BURN / APOAPSIS", result.execution.before_second_burn_diagnostic);
    print_burn_diagnostic("TERMINAL SHAPING BURN", result.execution.terminal_shaping_burn);
    print_stage_diagnostic("BEFORE TERMINAL CIRCULARIZATION", result.execution.before_terminal_circularization_diagnostic);
    print_burn_diagnostic("SECOND PLANNED / CIRCULARIZATION BURN", result.execution.second_planned_burn);
    print_stage_diagnostic("AFTER SECOND PLANNED BURN", result.execution.after_second_planned_diagnostic);
    print_burn_diagnostic("SECOND CORRECTION BURN", result.execution.second_correction_burn);
    std::cout << "  FINAL ERROR\n";
    std::cout << "    altitude                : " << result.final_altitude_error_m << " m\n";
    std::cout << "    speed                   : " << result.final_speed_error_m_per_s << " m/s\n";
    std::cout << "    semi-major axis error   : " << result.final_semi_major_axis_error_m << " m\n";
    std::cout << "    radial velocity         : " << result.final_radial_velocity_m_per_s << " m/s\n";
    std::cout << "    eccentricity            : " << result.execution.final_orbit.orbit.eccentricity << "\n";
    std::cout << "    position estimate       : " << result.final_position_estimation_error_m << " m\n";
    std::cout << "    velocity estimate       : " << result.final_velocity_estimation_error_m_per_s << " m/s\n";
}

void print_case(const CaseResult& result) {
    std::cout << result.config.name << "\n";
    std::cout << "  initial altitude          : " << result.config.initial_altitude_m << " m\n";
    std::cout << "  target altitude           : " << result.config.target_altitude_m << " m\n";
    std::cout << "  thrust scale              : " << result.config.actual_thrust_scale << "\n";
    std::cout << "  first burn scale          : " << result.config.first_burn_duration_scale << "\n";
    std::cout << "  second burn scale         : " << result.config.second_burn_duration_scale << "\n";
    std::cout << "  initial tangential dV     : " << result.config.initial_tangential_velocity_offset_m_per_s << " m/s\n";
    std::cout << "  apoapsis detected         : " << (result.execution.apoapsis_event_detected ? "YES" : "NO") << "\n";
    std::cout << "  first correction dV       : " << result.execution.first_correction_delta_v_m_per_s << " m/s\n";
    std::cout << "  second correction dV      : " << result.execution.second_correction_delta_v_m_per_s << " m/s\n";
    std::cout << "  first execution scale est : " << result.execution.first_burn_execution_scale_estimate << "\n";
    std::cout << "  adapted 2nd burn duration : " << result.execution.adapted_second_burn_duration_seconds << " s\n";
    std::cout << "  second execution scale est: " << result.execution.second_burn_execution_scale_estimate << "\n";
    std::cout << "  first performance residual: " << result.execution.first_burn_performance_residual_m_per_s << " m/s\n";
    std::cout << "  second performance residual: " << result.execution.second_burn_performance_residual_m_per_s << " m/s\n";
    std::cout << "  first duration scale est   : " << result.execution.first_burn_duration_scale_estimate << "\n";
    std::cout << "  second duration scale est  : " << result.execution.second_burn_duration_scale_estimate << "\n";
    std::cout << "  first reconstruction resid : " << result.execution.first_burn_reconstruction_residual_m_per_s << " m/s\n";
    std::cout << "  second reconstruction resid: " << result.execution.second_burn_reconstruction_residual_m_per_s << " m/s\n";
    std::cout << "  final altitude error      : " << result.final_altitude_error_m << " m\n";
    std::cout << "  final speed error         : " << result.final_speed_error_m_per_s << " m/s\n";
    std::cout << "  final eccentricity        : " << result.execution.final_orbit.orbit.eccentricity << "\n";
    std::cout << "  final position est error  : " << result.final_position_estimation_error_m << " m\n";
    std::cout << "  final velocity est error  : " << result.final_velocity_estimation_error_m_per_s << " m/s\n";
    std::cout << "  propellant used           : "
              << result.execution.first_planned_burn.propellant_consumed_kg
              + result.execution.first_correction_burn.propellant_consumed_kg
              + result.execution.second_planned_burn.propellant_consumed_kg
              + result.execution.second_correction_burn.propellant_consumed_kg << " kg\n";
    std::cout << "\n";
}

void validate_nominal(const CaseResult& result) {
    if (!result.execution.apoapsis_event_detected) throw std::runtime_error("Nominal apoapsis detection failed");
    if (std::abs(result.final_altitude_error_m) > 100.0) throw std::runtime_error("Nominal altitude validation failed");
    if (result.execution.final_orbit.orbit.eccentricity > 2e-3) throw std::runtime_error("Nominal eccentricity validation failed");
}

void validate_recovery(const std::vector<CaseResult>& results) {
    for (const auto& result : results) {
        if (!result.execution.apoapsis_event_detected) throw std::runtime_error("Recovery lost apoapsis detection");
        if (!std::isfinite(result.final_altitude_error_m) || !std::isfinite(result.final_speed_error_m_per_s)) {
            throw std::runtime_error("Recovery produced non-finite output");
        }
    }
}


struct MonteCarloSummary {
    std::size_t trials{0};
    std::size_t successful{0};
    std::size_t execution_failures{0};
    double success_rate_percent{0.0};
    double mean_abs_altitude_error_m{0.0};
    double p95_abs_altitude_error_m{0.0};
    double worst_abs_altitude_error_m{0.0};
    double mean_abs_speed_error_m_per_s{0.0};
    double worst_abs_speed_error_m_per_s{0.0};
};

struct MonteCarloFailureRecord {
    std::size_t trial_index{0};
    CaseConfig config{};
    double altitude_error_m{0.0};
    double speed_error_m_per_s{0.0};
    double semi_major_axis_error_m{0.0};
    double radial_velocity_m_per_s{0.0};
    double eccentricity{0.0};
};

struct TrialSample {
    CaseConfig config{};
    double abs_altitude_error_m{0.0};
    double abs_speed_error_m_per_s{0.0};
    bool success{false};
};

double pearson_correlation(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() != y.size() || x.size() < 2) return std::numeric_limits<double>::quiet_NaN();
    double sum_x = 0.0, sum_y = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) { sum_x += x[i]; sum_y += y[i]; }
    const double mean_x = sum_x / static_cast<double>(x.size());
    const double mean_y = sum_y / static_cast<double>(y.size());
    double numerator = 0.0, denom_x = 0.0, denom_y = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i) {
        const double dx = x[i] - mean_x;
        const double dy = y[i] - mean_y;
        numerator += dx * dy;
        denom_x += dx * dx;
        denom_y += dy * dy;
    }
    if (denom_x <= 0.0 || denom_y <= 0.0) return 0.0;
    return numerator / std::sqrt(denom_x * denom_y);
}

void print_failure_record(const MonteCarloFailureRecord& failure) {
    std::cout << "  trial " << failure.trial_index << " FAILING PARAMETERS\n"
              << "    target altitude             : " << failure.config.target_altitude_m << " m\n"
              << "    thrust scale                : " << failure.config.actual_thrust_scale << "\n"
              << "    Isp scale                   : " << failure.config.actual_isp_scale << "\n"
              << "    first burn duration scale   : " << failure.config.first_burn_duration_scale << "\n"
              << "    second burn duration scale  : " << failure.config.second_burn_duration_scale << "\n"
              << "    initial tangential dV       : " << failure.config.initial_tangential_velocity_offset_m_per_s << " m/s\n"
              << "    initial mass scale          : " << failure.config.initial_mass_scale << "\n"
              << "    sensor noise scale          : " << failure.config.sensor_noise_scale << "\n"
              << "    initial position std scale  : " << failure.config.initial_position_std_scale << "\n"
              << "    initial velocity std scale  : " << failure.config.initial_velocity_std_scale << "\n"
              << "    process noise scale         : " << failure.config.process_noise_scale << "\n"
              << "    final altitude error        : " << failure.altitude_error_m << " m\n"
              << "    final speed error           : " << failure.speed_error_m_per_s << " m/s\n"
              << "    final semi-major axis error : " << failure.semi_major_axis_error_m << " m\n"
              << "    final radial velocity       : " << failure.radial_velocity_m_per_s << " m/s\n"
              << "    final eccentricity          : " << failure.eccentricity << "\n";
}

double percentile95(std::vector<double> values) {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort(values.begin(), values.end());
    const double index = 0.95 * static_cast<double>(values.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(index));
    const auto hi = static_cast<std::size_t>(std::ceil(index));
    if (lo == hi) return values[lo];
    const double fraction = index - static_cast<double>(lo);
    return values[lo] * (1.0 - fraction) + values[hi] * fraction;
}

MonteCarloSummary run_monte_carlo(std::size_t trials) {
    std::cout << "MONTE CARLO START\n"
              << "  deterministic seed : 0x5A17C0DE\n"
              << "  trials planned      : " << trials << "\n"
              << "  progress             : enabled\n\n";
    std::cout.flush();
    std::mt19937_64 rng(0x5A17C0DEULL);
    auto uniform = [&rng](double lo, double hi) {
        std::uniform_real_distribution<double> distribution(lo, hi);
        return distribution(rng);
    };

    MonteCarloSummary summary{};
    summary.trials = trials;
    std::vector<double> abs_altitude_errors;
    std::vector<double> abs_speed_errors;
    abs_altitude_errors.reserve(trials);
    abs_speed_errors.reserve(trials);
    std::vector<TrialSample> samples;
    samples.reserve(trials);
    std::vector<MonteCarloFailureRecord> failures;

    constexpr double kSemiMajorAxisSuccessThresholdM = 25.0;
    constexpr double kEccentricitySuccessThreshold = 3.0e-4;

    for (std::size_t index = 0; index < trials; ++index) {
        CaseConfig cfg{};
        cfg.name = "MONTE_CARLO";
        cfg.initial_altitude_m = 400.0e3;
        cfg.target_altitude_m = uniform(450.0e3, 800.0e3);
        cfg.actual_thrust_scale = uniform(0.95, 1.05);
        cfg.actual_isp_scale = uniform(0.98, 1.02);
        cfg.first_burn_duration_scale = uniform(0.98, 1.02);
        cfg.second_burn_duration_scale = uniform(0.98, 1.02);
        cfg.initial_tangential_velocity_offset_m_per_s = uniform(-1.0, 1.0);
        cfg.initial_mass_scale = uniform(0.98, 1.02);
        cfg.sensor_noise_scale = uniform(0.75, 1.50);
        cfg.initial_position_std_scale = uniform(0.75, 1.50);
        cfg.initial_velocity_std_scale = uniform(0.75, 1.50);
        cfg.process_noise_scale = uniform(0.75, 1.50);
        cfg.noisy_sensors = true;

        try {
            const auto result = run_case(cfg);
            const double abs_altitude_error = std::abs(result.final_altitude_error_m);
            const double abs_speed_error = std::abs(result.final_speed_error_m_per_s);
            const double eccentricity = result.execution.final_orbit.orbit.eccentricity;
            const bool success =
                result.execution.apoapsis_event_detected &&
                std::isfinite(abs_altitude_error) &&
                std::isfinite(abs_speed_error) &&
                std::isfinite(result.final_semi_major_axis_error_m) &&
                std::isfinite(result.final_radial_velocity_m_per_s) &&
                std::isfinite(eccentricity) &&
                std::abs(result.final_semi_major_axis_error_m) <= kSemiMajorAxisSuccessThresholdM &&
                eccentricity <= kEccentricitySuccessThreshold;

            abs_altitude_errors.push_back(abs_altitude_error);
            abs_speed_errors.push_back(abs_speed_error);
            summary.mean_abs_altitude_error_m += abs_altitude_error;
            summary.mean_abs_speed_error_m_per_s += abs_speed_error;
            summary.worst_abs_altitude_error_m = std::max(summary.worst_abs_altitude_error_m, abs_altitude_error);
            summary.worst_abs_speed_error_m_per_s = std::max(summary.worst_abs_speed_error_m_per_s, abs_speed_error);
            if (success) {
                ++summary.successful;
            } else {
                failures.push_back({index + 1, cfg, result.final_altitude_error_m, result.final_speed_error_m_per_s, result.final_semi_major_axis_error_m, result.final_radial_velocity_m_per_s, eccentricity});
            }
            samples.push_back({cfg, abs_altitude_error, abs_speed_error, success});

            std::cout << "  trial " << (index + 1) << "/" << trials
                      << " : " << (success ? "PASS" : "FAIL")
                      << " | target=" << cfg.target_altitude_m
                      << " m | alt_err=" << result.final_altitude_error_m
                      << " m | speed_err=" << result.final_speed_error_m_per_s
                      << " m/s\n";
            std::cout.flush();
        } catch (const std::exception& error) {
            ++summary.execution_failures;
            std::cout << "  trial " << (index + 1) << "/" << trials
                      << " : EXECUTION_FAILURE | " << error.what() << "\n";
            std::cout.flush();
        }
    }

    const std::size_t completed = trials - summary.execution_failures;
    if (completed > 0) {
        summary.mean_abs_altitude_error_m /= static_cast<double>(completed);
        summary.mean_abs_speed_error_m_per_s /= static_cast<double>(completed);
        summary.success_rate_percent =
            100.0 * static_cast<double>(summary.successful) / static_cast<double>(trials);
        summary.p95_abs_altitude_error_m = percentile95(std::move(abs_altitude_errors));
    } else {
        summary.success_rate_percent = 0.0;
        summary.p95_abs_altitude_error_m = std::numeric_limits<double>::quiet_NaN();
    }

    std::cout << "\nFAILURE CHARACTERIZATION\n";
    if (failures.empty()) {
        std::cout << "  failing trials : NONE\n";
    } else {
        std::cout << "  failing trials : " << failures.size() << "\n";
        for (const auto& failure : failures) {
            print_failure_record(failure);
        }
    }

    if (!samples.empty()) {
        std::vector<double> target_altitudes;
        std::vector<double> thrust_scales;
        std::vector<double> isp_scales;
        std::vector<double> first_duration_scales;
        std::vector<double> second_duration_scales;
        std::vector<double> initial_dvs;
        std::vector<double> mass_scales;
        std::vector<double> sensor_scales;
        std::vector<double> position_scales;
        std::vector<double> velocity_scales;
        std::vector<double> process_scales;
        std::vector<double> altitude_errors;
        for (const auto& sample : samples) {
            target_altitudes.push_back(sample.config.target_altitude_m);
            thrust_scales.push_back(sample.config.actual_thrust_scale);
            isp_scales.push_back(sample.config.actual_isp_scale);
            first_duration_scales.push_back(sample.config.first_burn_duration_scale);
            second_duration_scales.push_back(sample.config.second_burn_duration_scale);
            initial_dvs.push_back(sample.config.initial_tangential_velocity_offset_m_per_s);
            mass_scales.push_back(sample.config.initial_mass_scale);
            sensor_scales.push_back(sample.config.sensor_noise_scale);
            position_scales.push_back(sample.config.initial_position_std_scale);
            velocity_scales.push_back(sample.config.initial_velocity_std_scale);
            process_scales.push_back(sample.config.process_noise_scale);
            altitude_errors.push_back(sample.abs_altitude_error_m);
        }
        std::cout << "  parameter correlation with |altitude error|\n";
        std::cout << "    target altitude            : " << pearson_correlation(target_altitudes, altitude_errors) << "\n";
        std::cout << "    thrust scale               : " << pearson_correlation(thrust_scales, altitude_errors) << "\n";
        std::cout << "    Isp scale                  : " << pearson_correlation(isp_scales, altitude_errors) << "\n";
        std::cout << "    first duration scale       : " << pearson_correlation(first_duration_scales, altitude_errors) << "\n";
        std::cout << "    second duration scale      : " << pearson_correlation(second_duration_scales, altitude_errors) << "\n";
        std::cout << "    initial tangential dV      : " << pearson_correlation(initial_dvs, altitude_errors) << "\n";
        std::cout << "    initial mass scale         : " << pearson_correlation(mass_scales, altitude_errors) << "\n";
        std::cout << "    sensor noise scale         : " << pearson_correlation(sensor_scales, altitude_errors) << "\n";
        std::cout << "    initial position std scale : " << pearson_correlation(position_scales, altitude_errors) << "\n";
        std::cout << "    initial velocity std scale : " << pearson_correlation(velocity_scales, altitude_errors) << "\n";
        std::cout << "    process noise scale        : " << pearson_correlation(process_scales, altitude_errors) << "\n";
    }
    return summary;
}

} // namespace




struct PerturbationCampaignResult {
    std::string name;
    double duration_seconds{0.0};
    double initial_perturbation_acceleration_m_per_s2{0.0};
    double final_altitude_delta_m{0.0};
    double final_speed_delta_m_per_s{0.0};
    double final_semi_major_axis_delta_m{0.0};
    double final_eccentricity_delta{0.0};
    double final_raan_delta_rad{0.0};
};

trishula::SimulationEngine make_environment_test_sim(
    const trishula::TrueState& state,
    double step_seconds,
    const trishula::PerturbationConfiguration& perturbation_configuration) {
    const trishula::CelestialBody earth(kMuEarth, kEarthRadius);
    const trishula::MainEngine engine(kNominalThrustN, kEngineIspS, kDryMassKg);
    const trishula::RcsModule rcs(kRcsForceN, kRcsIspS, kDryMassKg);
    return trishula::SimulationEngine(
        earth,
        trishula::Spacecraft(state, {100.0, 100.0, 100.0}),
        step_seconds,
        engine,
        rcs,
        trishula::SensorSuite{},
        perturbation_configuration);
}

PerturbationCampaignResult run_perturbation_case(
    const std::string& name,
    const trishula::TrueState& initial_state,
    double duration_seconds,
    double step_seconds,
    const trishula::PerturbationConfiguration& configuration) {
    auto reference = make_environment_test_sim(initial_state, step_seconds, {});
    auto perturbed = make_environment_test_sim(initial_state, step_seconds, configuration);
    const trishula::PerturbationModel perturbation_model(configuration);
    const auto initial_perturbation = perturbation_model.acceleration(
        perturbed.primary_body(), initial_state, 0.0);

    double remaining = duration_seconds;
    while (remaining > 1e-12) {
        const double dt = std::min(step_seconds, remaining);
        reference.step_for_duration({}, dt);
        perturbed.step_for_duration({}, dt);
        remaining -= dt;
    }

    trishula::EarthCenteredNavigator navigator;
    const trishula::EstimatedState reference_est = as_estimated(reference.spacecraft().state());
    const trishula::EstimatedState perturbed_est = as_estimated(perturbed.spacecraft().state());
    const auto reference_orbit = navigator.determine(reference_est, reference.primary_body());
    const auto perturbed_orbit = navigator.determine(perturbed_est, perturbed.primary_body());

    PerturbationCampaignResult result{};
    result.name = name;
    result.duration_seconds = duration_seconds;
    result.initial_perturbation_acceleration_m_per_s2 = initial_perturbation.magnitude();
    result.final_altitude_delta_m = perturbed_orbit.altitude_meters - reference_orbit.altitude_meters;
    result.final_speed_delta_m_per_s = perturbed_orbit.speed_m_per_s - reference_orbit.speed_m_per_s;
    result.final_semi_major_axis_delta_m =
        perturbed_orbit.orbit.semi_major_axis_meters - reference_orbit.orbit.semi_major_axis_meters;
    result.final_eccentricity_delta =
        perturbed_orbit.orbit.eccentricity - reference_orbit.orbit.eccentricity;
    result.final_raan_delta_rad =
        perturbed_orbit.orbit.right_ascension_of_ascending_node_rad -
        reference_orbit.orbit.right_ascension_of_ascending_node_rad;
    return result;
}


void run_perturbation_campaign() {
    std::cout << "\\nPERTURBED ENVIRONMENT CAMPAIGN\\n"
              << "==============================================================\\n"
              << "V0.9.19 adds optional J2, atmospheric drag, third-body lunar gravity,\\n"
              << "and solar-radiation-pressure perturbations. The models are simplified\\n"
              << "engineering models; they are not high-fidelity ephemeris/atmosphere models.\\n\\n";

    const double r400 = kEarthRadius + 400.0e3;
    const double r250 = kEarthRadius + 250.0e3;
    const double r100000 = kEarthRadius + 100000.0e3;
    const double v400 = std::sqrt(kMuEarth / r400);
    const double v250 = std::sqrt(kMuEarth / r250);
    const double v100000 = std::sqrt(kMuEarth / r100000);

    trishula::TrueState inclined{};
    inclined.position_meters = {r400, 0.0, 0.0};
    const double inclination = 45.0 * 3.14159265358979323846 / 180.0;
    inclined.velocity_m_per_s = {0.0, v400 * std::cos(inclination), v400 * std::sin(inclination)};
    inclined.mass_kg = kInitialMassKg;

    trishula::TrueState drag_state{};
    drag_state.position_meters = {r250, 0.0, 0.0};
    drag_state.velocity_m_per_s = {0.0, v250, 0.0};
    drag_state.mass_kg = kInitialMassKg;

    trishula::TrueState deep_space{};
    deep_space.position_meters = {r100000, 0.0, 0.0};
    deep_space.velocity_m_per_s = {0.0, v100000, 0.0};
    deep_space.mass_kg = kInitialMassKg;

    trishula::PerturbationConfiguration j2{};
    j2.enable_j2 = true;
    trishula::PerturbationConfiguration drag{};
    drag.enable_atmospheric_drag = true;
    trishula::PerturbationConfiguration moon{};
    moon.enable_third_body_moon = true;
    trishula::PerturbationConfiguration srp{};
    srp.enable_solar_radiation_pressure = true;

    const std::vector<PerturbationCampaignResult> results{
        run_perturbation_case("J2 - 45 deg orbit", inclined, 2.0 * 5544.855096, 10.0, j2),
        run_perturbation_case("Atmospheric drag - 250 km", drag_state, 2.0 * 5352.0, 10.0, drag),
        run_perturbation_case("Moon third-body gravity - 100000 km", deep_space, 86400.0, 60.0, moon),
        run_perturbation_case("Solar radiation pressure - 100000 km", deep_space, 86400.0, 60.0, srp),
    };

    for (const auto& result : results) {
        std::cout << "  " << result.name << "\n"
                  << "    duration                    : " << result.duration_seconds << " s\n"
                  << "    initial perturbation accel   : " << result.initial_perturbation_acceleration_m_per_s2 << " m/s^2\n"
                  << "    final altitude delta        : " << result.final_altitude_delta_m << " m\n"
                  << "    final speed delta            : " << result.final_speed_delta_m_per_s << " m/s\n"
                  << "    final semi-major-axis delta  : " << result.final_semi_major_axis_delta_m << " m\n"
                  << "    final eccentricity delta     : " << result.final_eccentricity_delta << "\n"
                  << "    final RAAN delta              : " << result.final_raan_delta_rad << " rad\n\n";
    }
    std::cout << "V0.9.22 status: PERTURBATION-AWARE DYNAMICS + TARGETING ENABLED\n"
              << "These results are environmental-model validation measurements.\n"
              << "They are not flight-dynamics qualification or high-fidelity ephemeris validation.\n";
}

struct CampaignSummary {
    std::size_t cases{0};
    std::size_t successful{0};
    std::size_t execution_failures{0};
    std::size_t raises{0};
    std::size_t lowerings{0};
    std::size_t raise_successes{0};
    std::size_t lowering_successes{0};
    double worst_abs_semi_major_axis_error_m{0.0};
    double worst_eccentricity{0.0};
};

CampaignSummary run_expanded_maneuver_campaign() {
    std::cout << "\\nEXPANDED ORBITAL MANEUVER CAMPAIGN\\n"
              << "==============================================================\\n"
              << "This campaign covers both orbit raising and lowering across\\n"
              << "multiple initial/target radii and propulsion/performance conditions.\\n\\n";
    const std::vector<CaseConfig> campaign{
        {"RAISE 400->500 km NOMINAL", 400.0e3, 500.0e3},
        {"RAISE 400->800 km NOMINAL", 400.0e3, 800.0e3},
        {"RAISE 500->750 km HIGH THRUST", 500.0e3, 750.0e3, 1.03, 1.0, 1.0, 0.0, true, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
        {"RAISE 350->650 km LOW THRUST", 350.0e3, 650.0e3, 0.97, 1.0, 1.0, 0.0, true, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
        {"RAISE 400->700 km MASS LOW", 400.0e3, 700.0e3, 1.0, 1.0, 1.0, 0.4, true, 1.0, 0.985, 1.15, 1.2, 1.15, 0.95},
        {"RAISE 400->700 km MASS HIGH", 400.0e3, 700.0e3, 1.0, 1.0, 1.0, -0.4, true, 0.99, 1.015, 1.2, 1.2, 1.2, 1.05},
        {"LOWER 800->500 km NOMINAL", 800.0e3, 500.0e3},
        {"LOWER 700->400 km HIGH THRUST", 700.0e3, 400.0e3, 1.03, 1.0, 1.0, 0.0, true, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
        {"LOWER 750->500 km LOW THRUST", 750.0e3, 500.0e3, 0.97, 1.0, 1.0, 0.0, true, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
        {"LOWER 650->350 km MASS LOW", 650.0e3, 350.0e3, 1.0, 1.01, 0.99, 0.5, true, 1.0, 0.985, 1.25, 1.2, 1.15, 0.95},
        {"RAISE 450->700 km Isp LOW", 450.0e3, 700.0e3, 1.0, 1.0, 1.0, 0.0, true, 0.985, 1.0, 1.1, 1.1, 1.1, 0.9},
        {"LOWER 800->600 km Isp HIGH", 800.0e3, 600.0e3, 1.0, 0.99, 1.01, -0.5, true, 1.015, 1.01, 1.35, 1.3, 1.3, 1.1}
    };

    CampaignSummary summary{};
    summary.cases = campaign.size();
    for (std::size_t i = 0; i < campaign.size(); ++i) {
        const auto& cfg = campaign[i];
        const bool raising = cfg.target_altitude_m > cfg.initial_altitude_m;
        if (raising) ++summary.raises; else ++summary.lowerings;
        try {
            const auto result = run_case(cfg);
            const double abs_a_error = std::abs(result.final_semi_major_axis_error_m);
            const double ecc = result.execution.final_orbit.orbit.eccentricity;
            const bool success = result.execution.apoapsis_event_detected &&
                                 std::isfinite(abs_a_error) &&
                                 std::isfinite(ecc) &&
                                 abs_a_error <= 25.0 &&
                                 ecc <= 3.0e-4;
            if (success) {
                ++summary.successful;
                if (raising) ++summary.raise_successes; else ++summary.lowering_successes;
            }
            summary.worst_abs_semi_major_axis_error_m = std::max(summary.worst_abs_semi_major_axis_error_m, abs_a_error);
            summary.worst_eccentricity = std::max(summary.worst_eccentricity, ecc);
            std::cout << "  case " << (i + 1) << "/" << campaign.size()
                      << " : " << (success ? "PASS" : "FAIL")
                      << " | " << cfg.name
                      << " | a_err=" << result.final_semi_major_axis_error_m
                      << " m | e=" << ecc
                      << " | alt_err=" << result.final_altitude_error_m
                      << " m | speed_err=" << result.final_speed_error_m_per_s
                      << " m/s\\n";
            std::cout.flush();
        } catch (const std::exception& error) {
            ++summary.execution_failures;
            std::cout << "  case " << (i + 1) << "/" << campaign.size()
                      << " : EXECUTION_FAILURE | " << cfg.name
                      << " | " << error.what() << "\\n";
            std::cout.flush();
        }
    }

    std::cout << "\\nEXPANDED CAMPAIGN SUMMARY\\n"
              << "  cases executed        : " << summary.cases << "\\n"
              << "  successful cases      : " << summary.successful << "\\n"
              << "  execution failures    : " << summary.execution_failures << "\\n"
              << "  raises                : " << summary.raise_successes << "/" << summary.raises << " passed\\n"
              << "  lowerings             : " << summary.lowering_successes << "/" << summary.lowerings << " passed\\n"
              << "  success rate          : " << (summary.cases ? 100.0 * static_cast<double>(summary.successful) / static_cast<double>(summary.cases) : 0.0) << " %\\n"
              << "  worst |a error|       : " << summary.worst_abs_semi_major_axis_error_m << " m\\n"
              << "  worst eccentricity    : " << summary.worst_eccentricity << "\\n"
              << "  acceptance            : |a error| <= 25 m, e <= 3e-4\\n\\n";
    return summary;
}

int main() {
    try {
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.22 - Perturbation-Aware Maneuver Targeting\n";
        std::cout << "==============================================================\n\n";
        std::cout << "The recovery loop now uses simulated sensor measurements and\n"
                     "the covariance-based estimator, predictive targeting, and actuator-telemetry propulsive impulse reconstruction.\n\n";

        const std::vector<CaseConfig> cases{
            {"CASE A - NOMINAL", 400.0e3, 500.0e3},
            {"CASE B - HIGHER RAISE", 400.0e3, 800.0e3},
            {"CASE C - LOWER THRUST (95%)", 400.0e3, 500.0e3, 0.95, 1.0, 1.0, 0.0},
            {"CASE D - OVERBURN +2%", 400.0e3, 500.0e3, 1.0, 1.02, 1.02, 0.0},
            {"CASE E - UNDERBURN -2%", 400.0e3, 500.0e3, 1.0, 0.98, 0.98, 0.0},
            {"CASE F - INITIAL VELOCITY +1 m/s", 400.0e3, 500.0e3, 1.0, 1.0, 1.0, 1.0},
            {"CASE G - INITIAL VELOCITY -1 m/s", 400.0e3, 500.0e3, 1.0, 1.0, 1.0, -1.0},
            {"CASE H - SENSOR-ONLY NOMINAL", 400.0e3, 500.0e3, 1.0, 1.0, 1.0, 0.0, true},
        };

        std::vector<CaseResult> results;
        results.reserve(cases.size());
        for (const auto& cfg : cases) {
            auto result = run_case(cfg);
            print_case(result);
            results.push_back(std::move(result));
        }

        validate_nominal(results.front());
        if (!std::isfinite(results.front().final_position_estimation_error_m) ||
            !std::isfinite(results.front().final_velocity_estimation_error_m_per_s)) {
            throw std::runtime_error("Estimator validation produced non-finite error");
        }
        validate_recovery(results);

        std::cout << "RECOVERY SUMMARY\n";
        std::cout << "  scenarios executed : " << results.size() << "\n";
        std::cout << "  nominal baseline   : PASS\n";
        std::cout << "  correction loop    : EXECUTED\n";
        std::cout << "  sensor path        : ENABLED\n";
        std::cout << "  recovery outputs   : MEASURED\n";
        std::cout << "  estimator status   : COVARIANCE FILTER ENABLED\n";
        std::cout << "  adaptive execution : ENABLED\n"
                  << "  performance ID     : ACTUATOR IMPULSE + STATE TRANSITION\n\n";
        std::cout << "This milestone reconstructs propulsive impulse from the actual\n"
                     "simulated actuator thrust integration and cross-checks it against\n"
                     "the estimated pre/post state transition. The nominal reference uses\n"
                     "the measured burn duration so duration changes are separated from\n"
                     "effective thrust performance.\n\n";

        constexpr std::size_t kMonteCarloTrials = 16;
        const auto monte_carlo = run_monte_carlo(kMonteCarloTrials);

        const CaseConfig failure_trial_1{
            "REPRO TRIAL 1", 400.0e3, 734520.615596, 1.046419, 0.998978, 1.002005,
            0.675967, true, 0.995361, 0.986670, 1.349076, 1.173917, 1.216921, 0.931009};
        const CaseConfig failure_trial_3{
            "REPRO TRIAL 3", 400.0e3, 701541.007411, 1.036298, 1.018155, 0.982355,
            -0.739813, true, 0.995925, 0.980931, 1.407724, 1.321175, 1.180919, 0.846342};

        const auto reproduced_1 = run_case(failure_trial_1);
        const auto reproduced_3 = run_case(failure_trial_3);
        print_failure_reproduction("TRIAL 1", reproduced_1);
        print_failure_reproduction("TRIAL 3", reproduced_3);

        std::cout << "MONTE CARLO SUMMARY\n";
        std::cout << "  deterministic seed    : 0x5A17C0DE\n";
        std::cout << "  trials executed       : " << monte_carlo.trials << "\n";
        std::cout << "  successful trials     : " << monte_carlo.successful << "\n";
        std::cout << "  execution failures    : " << monte_carlo.execution_failures << "\n";
        std::cout << "  success rate          : " << monte_carlo.success_rate_percent << " %\n";
        std::cout << "  mean |altitude error| : " << monte_carlo.mean_abs_altitude_error_m << " m\n";
        std::cout << "  p95 |altitude error|  : " << monte_carlo.p95_abs_altitude_error_m << " m\n";
        std::cout << "  worst |altitude error|: " << monte_carlo.worst_abs_altitude_error_m << " m\n";
        std::cout << "  mean |speed error|    : " << monte_carlo.mean_abs_speed_error_m_per_s << " m/s\n";
        std::cout << "  worst |speed error|   : " << monte_carlo.worst_abs_speed_error_m_per_s << " m/s\n";
        std::cout << "  success thresholds    : |semi-major axis error| <= 25 m, e <= 3e-4 (instantaneous altitude/speed are diagnostics)\n\n";
        const auto campaign = run_expanded_maneuver_campaign();
        std::cout << "V0.9.18 adds direction-aware terminal-event handling: raising maneuvers use the next apoapsis, while lowering maneuvers skip the first periapsis and use the following apoapsis for terminal shaping.\n"
                     "Campaign acceptance is based on final orbital state: |semi-major axis error| <= 25 m and eccentricity <= 3e-4.\n"
                     "The Monte Carlo and campaign results are validation measurements, not claims of flight qualification or universal mission robustness.\n";

        run_perturbation_campaign();

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RECOVERY EXECUTION FAILURE: " << error.what() << '\n';
        return 1;
    }
}
