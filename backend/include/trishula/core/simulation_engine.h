#pragma once

#include "trishula/core/simulation_clock.h"
#include "trishula/environment/celestial_body.h"
#include "trishula/physics/attitude_integrator.h"
#include "trishula/physics/dynamics.h"
#include "trishula/physics/integrator.h"
#include "trishula/physics/velocity_verlet_integrator.h"
#include "trishula/physics/gravity.h"
#include "trishula/physics/perturbations.h"
#include "trishula/propulsion/propulsion.h"
#include "trishula/sensors/sensors.h"
#include "trishula/vehicle/spacecraft.h"

namespace trishula {

class SimulationEngine {
public:
    SimulationEngine(
        CelestialBody primary_body,
        Spacecraft spacecraft,
        double time_step_seconds,
        MainEngine main_engine,
        RcsModule rcs_module,
        SensorSuite sensor_suite = {},
        PerturbationConfiguration perturbation_configuration = {});

    void step(const PropulsionCommand& propulsion_command = {});
    void step_for_duration(const PropulsionCommand& propulsion_command, double duration_seconds);

    [[nodiscard]] const SimulationClock& clock() const noexcept;
    [[nodiscard]] const CelestialBody& primary_body() const noexcept;
    [[nodiscard]] const Spacecraft& spacecraft() const noexcept;
    [[nodiscard]] const MainEngine& main_engine() const noexcept;
    [[nodiscard]] const RcsModule& rcs_module() const noexcept;
    [[nodiscard]] const PropulsionOutput& last_propulsion_output() const noexcept;
    [[nodiscard]] const SensorSuite& sensor_suite() const noexcept;
    [[nodiscard]] const PerturbationModel& perturbation_model() const noexcept;
    [[nodiscard]] const SensorData& last_sensor_data() const noexcept;

private:
    SimulationClock clock_;
    CelestialBody primary_body_;
    Spacecraft spacecraft_;
    MainEngine main_engine_;
    RcsModule rcs_module_;
    PropulsionOutput last_propulsion_output_{};
    GravityModel gravity_model_;
    TranslationalDynamics dynamics_;
    VelocityVerletIntegrator translational_integrator_;
    AttitudeEulerIntegrator attitude_integrator_;
    SensorSuite sensor_suite_;
    PerturbationModel perturbation_model_;
    SensorData last_sensor_data_{};
};

} // namespace trishula
