#include "trishula/maneuver/lunar_orbit_insertion.h"
#include "trishula/core/vector3.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
constexpr double kMuMoon = 4.9048695e12;
constexpr double kMoonRadius = 1.7374e6;
constexpr double kPeriluneAlt = 100.0e3;
constexpr double kApoluneAlt = 1000.0e3;
constexpr double kTargetRp = kMoonRadius + kPeriluneAlt;
constexpr double kTargetRa = kMoonRadius + kApoluneAlt;
constexpr double kVinf = 787.785675;
constexpr double kThrust = 5.0e6;
constexpr double kIsp = 450.0;
constexpr double kInitialMass = 24859.219435;
constexpr double kDryMass = 20000.0;
constexpr double kG0 = 9.80665;
constexpr double kJ2Moon = 2.03428e-4;
constexpr double kDt = 2.0;
constexpr int kRevolutions = 5;

struct State {
    trishula::Vector3 r{};
    trishula::Vector3 v{};
    double mass{0.0};
    double time{0.0};
};

trishula::Vector3 j2_accel(const trishula::Vector3& r) {
    const double x = r.x, y = r.y, z = r.z;
    const double rr = r.magnitude();
    if (rr <= kMoonRadius) return {};
    const double z2 = z * z;
    const double r2 = rr * rr;
    const double factor = 1.5 * kJ2Moon * kMuMoon * kMoonRadius * kMoonRadius / std::pow(rr, 5);
    const double common = 5.0 * z2 / r2;
    return {
        factor * x * (common - 1.0),
        factor * y * (common - 1.0),
        factor * z * (common - 3.0)
    };
}

trishula::Vector3 acceleration(const State& s, const trishula::Vector3& thrust, bool lunar_j2) {
    const double rr = s.r.magnitude();
    if (rr <= kMoonRadius) throw std::runtime_error("Moon collision detected");
    trishula::Vector3 a = s.r * (-kMuMoon / (rr * rr * rr));
    if (lunar_j2) a += j2_accel(s.r);
    if (s.mass > 0.0) a += thrust / s.mass;
    return a;
}

void step(State& s, double dt, const trishula::Vector3& thrust, bool lunar_j2) {
    const auto a0 = acceleration(s, thrust, lunar_j2);
    const auto r1 = s.r + s.v * dt + a0 * (0.5 * dt * dt);
    const double thrust_mag = thrust.magnitude();
    const double mdot = thrust_mag / (kIsp * kG0);
    const double consumed = std::min(std::max(0.0, s.mass - kDryMass), mdot * dt);
    State provisional = s;
    provisional.r = r1;
    provisional.v = s.v + a0 * dt;
    provisional.mass = s.mass - consumed;
    const auto a1 = acceleration(provisional, thrust, lunar_j2);
    s.r = r1;
    s.v = s.v + (a0 + a1) * (0.5 * dt);
    s.mass = provisional.mass;
    s.time += dt;
}

double radial_velocity(const State& s) {
    return s.r.dot(s.v) / s.r.magnitude();
}

struct ApsisEvent {
    double t{0.0};
    double radius{0.0};
    double speed{0.0};
    bool periapsis{false};
};

std::vector<ApsisEvent> propagate_to_apsides(State& s, int target_events, bool lunar_j2) {
    std::vector<ApsisEvent> events;
    double prev_radial = radial_velocity(s);
    for (int i = 0; i < 300000 && static_cast<int>(events.size()) < target_events; ++i) {
        step(s, kDt, {}, lunar_j2);
        const double current_radial = radial_velocity(s);
        if (prev_radial < 0.0 && current_radial >= 0.0) {
            events.push_back({s.time, s.r.magnitude(), s.v.magnitude(), true});
        } else if (prev_radial > 0.0 && current_radial <= 0.0) {
            events.push_back({s.time, s.r.magnitude(), s.v.magnitude(), false});
        }
        prev_radial = current_radial;
    }
    if (static_cast<int>(events.size()) != target_events) {
        throw std::runtime_error("Failed to detect required lunar orbit apsides");
    }
    return events;
}


ApsisEvent propagate_to_next_apsis(State& s, bool target_periapsis, bool lunar_j2) {
    double prev_radial = radial_velocity(s);
    for (int i = 0; i < 300000; ++i) {
        step(s, kDt, {}, lunar_j2);
        const double current_radial = radial_velocity(s);
        const bool peri = prev_radial < 0.0 && current_radial >= 0.0;
        const bool apo = prev_radial > 0.0 && current_radial <= 0.0;
        if ((target_periapsis && peri) || (!target_periapsis && apo)) {
            return {s.time, s.r.magnitude(), s.v.magnitude(), target_periapsis};
        }
        prev_radial = current_radial;
    }
    throw std::runtime_error("Failed to detect requested lunar apsis");
}

State make_post_loi_orbit() {
    const double a = 0.5 * (kTargetRp + kTargetRa);
    const double vp = std::sqrt(kMuMoon * (2.0 / kTargetRp - 1.0 / a));
    return {{kTargetRp, 0.0, 0.0}, {0.0, vp, 0.0}, kInitialMass, 0.0};
}

void apply_tangential_impulse(State& s, double dv) {
    const auto direction = s.v.normalized();
    s.v += direction * dv;
}

double semi_major_axis(const State& s) {
    const double energy = trishula::specific_orbital_energy(kMuMoon, s.r, s.v);
    return trishula::semi_major_axis_from_energy(kMuMoon, energy);
}

double eccentricity(const State& s) {
    return trishula::eccentricity_from_state(kMuMoon, s.r, s.v);
}

} // namespace

int main() {
    try {
        const auto a_target = 0.5 * (kTargetRp + kTargetRa);
        const auto target_period = 2.0 * std::acos(-1.0) * std::sqrt(a_target * a_target * a_target / kMuMoon);

        State baseline = make_post_loi_orbit();
        const auto baseline_events = propagate_to_apsides(baseline, kRevolutions * 2, false);
        std::vector<double> baseline_periods;
        for (std::size_t i = 2; i < baseline_events.size(); i += 2) {
            baseline_periods.push_back(baseline_events[i].t - baseline_events[i - 2].t);
        }

        const double baseline_a = semi_major_axis(baseline);
        const double baseline_e = eccentricity(baseline);
        const auto baseline_peri = std::find_if(baseline_events.begin(), baseline_events.end(), [](const ApsisEvent& e){ return e.periapsis; });
        const auto baseline_apo = std::find_if(baseline_events.begin(), baseline_events.end(), [](const ApsisEvent& e){ return !e.periapsis; });
        if (baseline_peri == baseline_events.end() || baseline_apo == baseline_events.end()) throw std::runtime_error("Missing baseline apsis events");
        const double baseline_rp = baseline_peri->radius;
        const double baseline_ra = baseline_apo->radius;

        // Simulate a small deterministic station-keeping disturbance at apolune,
        // then close the loop with a bounded feedback correction at the next apolune.
        State maintenance = make_post_loi_orbit();
        const auto initial_apo = propagate_to_next_apsis(maintenance, false, false);
        apply_tangential_impulse(maintenance, -2.0); // deterministic retrograde disturbance
        const auto next_apo_before = propagate_to_next_apsis(maintenance, false, false);
        const double disturbed_ra = next_apo_before.radius;
        const double apolune_error = kTargetRa - disturbed_ra;
        const double correction_dv = std::clamp(0.5 * apolune_error / 1000.0, -2.0, 2.0);
        apply_tangential_impulse(maintenance, correction_dv);
        const auto next_apo_after = propagate_to_next_apsis(maintenance, false, false);
        const double corrected_ra = next_apo_after.radius;
        (void)initial_apo;
        State j2 = make_post_loi_orbit();
        const auto j2_events = propagate_to_apsides(j2, kRevolutions * 2, true);
        const auto j2_peri_first = std::find_if(j2_events.begin(), j2_events.end(), [](const ApsisEvent& e){ return e.periapsis; });
        const auto j2_apo_first = std::find_if(j2_events.begin(), j2_events.end(), [](const ApsisEvent& e){ return !e.periapsis; });
        const auto j2_peri_last = std::find_if(j2_events.rbegin(), j2_events.rend(), [](const ApsisEvent& e){ return e.periapsis; });
        const auto j2_apo_last = std::find_if(j2_events.rbegin(), j2_events.rend(), [](const ApsisEvent& e){ return !e.periapsis; });
        if (j2_peri_first == j2_events.end() || j2_apo_first == j2_events.end() || j2_peri_last == j2_events.rend() || j2_apo_last == j2_events.rend()) throw std::runtime_error("Missing J2 apsis events");
        const double j2_rp_drift = j2_peri_last->radius - j2_peri_first->radius;
        const double j2_ra_drift = j2_apo_last->radius - j2_apo_first->radius;
        const double j2_a_final = semi_major_axis(j2);
        const double j2_e_final = eccentricity(j2);

        const bool period_ok = std::abs(baseline_periods.back() - target_period) < 5.0;
        const bool apsis_ok = std::abs(baseline_rp - kTargetRp) < 1000.0 && std::abs(baseline_ra - kTargetRa) < 2000.0;
        const bool bound_ok = baseline_a > kMoonRadius && baseline_e < 0.25;
        const bool maintenance_effective = std::abs(corrected_ra - kTargetRa) < std::abs(disturbed_ra - kTargetRa);
        const bool j2_stable = j2_a_final > kMoonRadius && j2_e_final < 0.25 && std::abs(j2_rp_drift) < 25000.0;
        const bool pass = period_ok && apsis_ok && bound_ok && maintenance_effective && j2_stable;

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.27 - Lunar Orbit Operations\n";
        std::cout << "==============================================================\n";
        std::cout << "  target perilune altitude          : " << kPeriluneAlt << " m\n";
        std::cout << "  target apolune altitude           : " << kApoluneAlt << " m\n";
        std::cout << "  target orbital period              : " << target_period << " s\n";
        std::cout << "  revolutions simulated              : " << kRevolutions << "\n";
        std::cout << "  baseline semi-major axis           : " << baseline_a / 1000.0 << " km\n";
        std::cout << "  baseline eccentricity              : " << baseline_e << "\n";
        std::cout << "  baseline perilune                  : " << (baseline_rp - kMoonRadius) / 1000.0 << " km\n";
        std::cout << "  baseline apolune                   : " << (baseline_ra - kMoonRadius) / 1000.0 << " km\n";
        std::cout << "  final measured period              : " << baseline_periods.back() << " s\n";
        std::cout << "  maintenance disturbance            : -2.000000 m/s\n";
        std::cout << "  disturbed apolune                  : " << (disturbed_ra - kMoonRadius) / 1000.0 << " km\n";
        std::cout << "  maintenance correction             : " << correction_dv << " m/s\n";
        std::cout << "  corrected apolune                  : " << (corrected_ra - kMoonRadius) / 1000.0 << " km\n";
        std::cout << "  J2 final semi-major axis           : " << j2_a_final / 1000.0 << " km\n";
        std::cout << "  J2 final eccentricity              : " << j2_e_final << "\n";
        std::cout << "  J2 perilune drift over campaign    : " << j2_rp_drift / 1000.0 << " km\n";
        std::cout << "  J2 apolune drift diagnostic        : " << j2_ra_drift / 1000.0 << " km\n";
        std::cout << "  repeated apsis detection            : " << (baseline_events.size() >= 10 ? "YES" : "NO") << "\n";
        std::cout << "  bounded orbit maintained            : " << (bound_ok ? "YES" : "NO") << "\n";
        std::cout << "  station-keeping correction          : " << (maintenance_effective ? "EFFECTIVE" : "INSUFFICIENT") << "\n";
        std::cout << "  lunar J2 propagation                : " << (j2_stable ? "STABLE" : "UNSTABLE") << "\n";

        if (!pass) throw std::runtime_error("lunar orbit operations acceptance failed");
        std::cout << "\nV0.9.27 lunar orbit operations campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.27 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
