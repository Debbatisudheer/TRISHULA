#pragma once

#include "trishula/core/vector3.h"
#include "trishula/physics/state.h"
#include "trishula/environment/celestial_body.h"

namespace trishula {

struct PerturbationConfiguration {
    bool enable_j2{false};
    double j2{1.08262668e-3};
    double body_rotation_rate_rad_s{7.2921150e-5};

    bool enable_atmospheric_drag{false};
    double drag_coefficient{2.2};
    double reference_area_m2{10.0};
    double reference_density_kg_m3{3.0e-11};
    double reference_altitude_m{250000.0};
    double density_scale_height_m{60000.0};

    bool enable_third_body_moon{false};
    double moon_gravitational_parameter_m3_s2{4.9048695e12};
    double moon_orbit_radius_m{384400000.0};
    double moon_orbit_period_s{27.321661 * 86400.0};
    double moon_phase_rad{0.0};

    bool enable_solar_radiation_pressure{false};
    double solar_pressure_n_m2{4.56e-6};
    double solar_reflectivity_coefficient{1.3};
    double solar_area_m2{10.0};
    double solar_orbit_radius_m{1.495978707e11};
    double solar_orbit_period_s{365.256363004 * 86400.0};
    double solar_phase_rad{0.0};
};

class PerturbationModel {
public:
    explicit PerturbationModel(PerturbationConfiguration configuration = {});

    [[nodiscard]] Vector3 acceleration(
        const CelestialBody& primary_body,
        const TrueState& state,
        double time_seconds) const;

    [[nodiscard]] Vector3 j2_acceleration(
        const CelestialBody& primary_body,
        const Vector3& relative_position_m) const;

    [[nodiscard]] Vector3 atmospheric_drag_acceleration(
        const CelestialBody& primary_body,
        const TrueState& state) const;

    [[nodiscard]] Vector3 third_body_moon_acceleration(
        const CelestialBody& primary_body,
        const Vector3& relative_position_m,
        double time_seconds) const;

    [[nodiscard]] Vector3 solar_radiation_pressure_acceleration(
        const TrueState& state,
        double time_seconds) const;

    [[nodiscard]] const PerturbationConfiguration& configuration() const noexcept;

private:
    PerturbationConfiguration configuration_{};
};

} // namespace trishula
