#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "trishula/core/simulation_engine.h"
#include "trishula/estimation/state_estimator.h"
#include "trishula/maneuver/finite_burn_predictor.h"
#include "trishula/physics/perturbations.h"
#include "trishula/vehicle/spacecraft.h"

namespace {
constexpr double kMuEarth = 3.986004418e14;
constexpr double kEarthRadius = 6.371e6;
constexpr double kInitialMassKg = 1000.0;
constexpr double kDryMassKg = 800.0;
constexpr double kThrustN = 20000.0;
constexpr double kIspS = 300.0;
constexpr double kRcsForceN = 20.0;
constexpr double kRcsIspS = 120.0;
constexpr double kStepS = 0.1;

struct Result {
    std::string name;
    bool valid{false};
    double instantaneous_dv{0.0};
    double coast_aware_dv{0.0};
    double delta_dv{0.0};
    double coast_prediction_error_m{0.0};
};

trishula::SimulationEngine make_engine(const trishula::PerturbationConfiguration& cfg) {
    const double radius = kEarthRadius + 400.0e3;
    const double speed = std::sqrt(kMuEarth / radius);
    trishula::TrueState state{};
    state.position_meters = {radius, 0.0, 0.0};
    state.velocity_m_per_s = {0.0, speed, 0.0};
    state.mass_kg = kInitialMassKg;
    return trishula::SimulationEngine(
        trishula::CelestialBody(kMuEarth, kEarthRadius),
        trishula::Spacecraft(state, {100.0, 100.0, 100.0}),
        kStepS,
        trishula::MainEngine(kThrustN, kIspS, kDryMassKg),
        trishula::RcsModule(kRcsForceN, kRcsIspS, kDryMassKg),
        trishula::SensorSuite{}, cfg);
}

trishula::StateEstimator make_estimator() {
    const double radius = kEarthRadius + 400.0e3;
    const double speed = std::sqrt(kMuEarth / radius);
    trishula::EstimatorConfiguration cfg{};
    cfg.enable_covariance_filter = false;
    return trishula::StateEstimator({radius, 0.0, 0.0}, {0.0, speed, 0.0}, {}, cfg);
}

Result run_case(const std::string& name, trishula::PerturbationConfiguration cfg,
                double maximum_coast_seconds) {
    auto engine = make_engine(cfg);
    auto estimator = make_estimator();
    trishula::FiniteBurnPredictor predictor(kStepS);
    const double target_radius = kEarthRadius + 350.0e3;

    const auto instantaneous = predictor.solve_for_periapsis(
        engine, estimator, target_radius, 100.0);
    const auto coast_aware = predictor.solve_for_periapsis_after_coast(
        engine, estimator, target_radius, 100.0, maximum_coast_seconds);

    Result result{};
    result.name = name;
    result.valid = instantaneous.valid && coast_aware.valid;
    result.instantaneous_dv = instantaneous.delta_v_m_per_s;
    result.coast_aware_dv = coast_aware.delta_v_m_per_s;
    result.delta_dv = std::abs(result.instantaneous_dv - result.coast_aware_dv);
    result.coast_prediction_error_m = coast_aware.prediction_error_meters;
    return result;
}

} // namespace

int main() {
    try {
        std::cout << std::fixed << std::setprecision(9);
        std::cout << "TRISHULA V0.9.22 - Perturbation-Aware Targeting Campaign\n";
        std::cout << "==============================================================\n";
        std::cout << "Comparing instantaneous targeting with explicit perturbed-coast targeting.\n\n";

        trishula::PerturbationConfiguration none{};
        trishula::PerturbationConfiguration j2{};
        j2.enable_j2 = true;
        trishula::PerturbationConfiguration drag{};
        drag.enable_atmospheric_drag = true;

        // Stress-scaled copies are used only as numerical sensitivity cases for
        // weak perturbations whose normal acceleration can be below the finite-\n        //difference threshold in this compact regression test. They are not\n        //physical parameter claims.
        trishula::PerturbationConfiguration moon{};
        moon.enable_third_body_moon = true;
        moon.moon_gravitational_parameter_m3_s2 *= 20.0;

        trishula::PerturbationConfiguration srp{};
        srp.enable_solar_radiation_pressure = true;
        srp.solar_pressure_n_m2 *= 10000.0;

        std::vector<Result> results;
        results.push_back(run_case("NO PERTURBATION", none, 1800.0));
        results.push_back(run_case("J2 ACTIVE", j2, 1800.0));
        results.push_back(run_case("DRAG ACTIVE", drag, 1800.0));
        results.push_back(run_case("MOON ACTIVE (stress scale)", moon, 1800.0));
        results.push_back(run_case("SRP ACTIVE (stress scale)", srp, 1800.0));

        for (const auto& result : results) {
            std::cout << "  " << result.name << " : "
                      << (result.valid ? "SOLVED" : "FAILED")
                      << " | instantaneous_dV=" << result.instantaneous_dv
                      << " m/s | coast_aware_dV=" << result.coast_aware_dv
                      << " m/s | delta=" << result.delta_dv
                      << " m/s | prediction_error=" << result.coast_prediction_error_m
                      << " m\n";
        }

        if (!results.front().valid) {
            throw std::runtime_error("Baseline predictor case did not solve");
        }
        for (std::size_t i = 1; i < results.size(); ++i) {
            if (!results[i].valid) {
                throw std::runtime_error(results[i].name + " did not solve");
            }
            if (!(results[i].delta_dv > 1.0e-8)) {
                throw std::runtime_error(results[i].name +
                                         " did not change the coast-aware targeting solution");
            }
        }

        std::cout << "\nV0.9.22 perturbation-aware targeting campaign PASSED.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CAMPAIGN FAILURE: " << error.what() << '\n';
        return 1;
    }
}
