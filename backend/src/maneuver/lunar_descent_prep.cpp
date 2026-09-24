#include "trishula/maneuver/lunar_descent_prep.h"

#include <cmath>
#include <stdexcept>

namespace trishula {
namespace {

double vis_viva(double mu, double radius, double semi_major_axis) {
    return std::sqrt(mu * (2.0 / radius - 1.0 / semi_major_axis));
}

} // namespace

LunarDescentPreparationPlan LunarDescentPreparationPlanner::plan(
    double moon_mu,
    double moon_radius,
    double initial_perilune_altitude,
    double initial_apolune_altitude,
    double target_perilune_altitude,
    double target_apolune_altitude) const {
    if (moon_mu <= 0.0 || moon_radius <= 0.0 ||
        initial_perilune_altitude < 0.0 || initial_apolune_altitude <= initial_perilune_altitude ||
        target_perilune_altitude < 0.0 || target_apolune_altitude <= target_perilune_altitude) {
        throw std::invalid_argument("Invalid lunar descent preparation geometry");
    }
    if (target_apolune_altitude >= initial_apolune_altitude ||
        target_perilune_altitude >= initial_perilune_altitude) {
        throw std::invalid_argument("Descent preparation target must be lower than initial orbit");
    }

    const double rpi = moon_radius + initial_perilune_altitude;
    const double rai = moon_radius + initial_apolune_altitude;
    const double rpf = moon_radius + target_perilune_altitude;
    const double raf = moon_radius + target_apolune_altitude;

    // Burn 1 occurs at the current apolune. It injects the vehicle into the
    // transfer ellipse whose apolune remains rai and whose perilune is rpf.
    const double a1 = 0.5 * (rai + rpf);
    const double v_ap_initial = vis_viva(moon_mu, rai, 0.5 * (rpi + rai));
    const double v_ap_transfer = vis_viva(moon_mu, rai, a1);
    const double dv1 = std::max(0.0, v_ap_initial - v_ap_transfer);

    // Burn 2 occurs at the new low perilune and circularizes/reshapes to the
    // requested final low-altitude ellipse with apolune raf.
    const double a2 = 0.5 * (rpf + raf);
    const double v_peri_transfer = vis_viva(moon_mu, rpf, a1);
    const double v_peri_final = vis_viva(moon_mu, rpf, a2);
    const double dv2 = std::max(0.0, v_peri_transfer - v_peri_final);

    LunarDescentPreparationPlan out{};
    out.moon_gravitational_parameter_m3_s2 = moon_mu;
    out.moon_radius_meters = moon_radius;
    out.initial_perilune_altitude_meters = initial_perilune_altitude;
    out.initial_apolune_altitude_meters = initial_apolune_altitude;
    out.target_perilune_altitude_meters = target_perilune_altitude;
    out.target_apolune_altitude_meters = target_apolune_altitude;
    out.first_burn_delta_v_m_per_s = dv1;
    out.second_burn_delta_v_m_per_s = dv2;
    out.total_delta_v_m_per_s = dv1 + dv2;
    out.landing_interface_altitude_meters = target_perilune_altitude;
    out.valid = true;
    return out;
}

} // namespace trishula
