#include "trishula/maneuver/lunar_descent_prep.h"
#include "trishula/maneuver/lunar_orbit_insertion.h"
#include "trishula/core/vector3.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
constexpr double kMuMoon = 4.9048695e12;
constexpr double kMoonRadius = 1.7374e6;
constexpr double kInitialRpAlt = 100.0e3;
constexpr double kInitialRaAlt = 1000.0e3;
constexpr double kTargetRpAlt = 30.0e3;
constexpr double kTargetRaAlt = 100.0e3;
constexpr double kDryMass = 20000.0;
constexpr double kInitialMass = 24859.219435;
constexpr double kThrust = 110000.0;
constexpr double kIsp = 320.0;
constexpr double kG0 = 9.80665;
constexpr double kDt = 0.25;

struct State { trishula::Vector3 r; trishula::Vector3 v; double mass; double time; };

double energy(const State& s) { return trishula::specific_orbital_energy(kMuMoon, s.r, s.v); }
double sma(const State& s) { return trishula::semi_major_axis_from_energy(kMuMoon, energy(s)); }
double ecc(const State& s) { return trishula::eccentricity_from_state(kMuMoon, s.r, s.v); }

double radial_velocity(const State& s) { return s.r.dot(s.v) / s.r.magnitude(); }

void step(State& s, double dt, double thrust_signed) {
    const double rmag = s.r.magnitude();
    const auto radial = s.r.normalized();
    const auto tangential = trishula::Vector3{-radial.y, radial.x, 0.0}.normalized();
    const auto thrust = tangential * thrust_signed;
    const auto a_grav = s.r * (-kMuMoon / (rmag*rmag*rmag));
    const auto a0 = a_grav + (s.mass > kDryMass ? thrust / s.mass : trishula::Vector3{});
    const auto r1 = s.r + s.v*dt + a0*(0.5*dt*dt);
    State tmp=s;
    tmp.r=r1;
    const double mdot=std::abs(thrust_signed)/(kIsp*kG0);
    tmp.mass=std::max(kDryMass, s.mass-std::min(std::max(0.0,s.mass-kDryMass), mdot*dt));
    tmp.v=s.v+a0*dt;
    const auto a1 = tmp.r * (-kMuMoon / std::pow(tmp.r.magnitude(),3.0)) + (tmp.mass>kDryMass ? thrust/tmp.mass : trishula::Vector3{});
    s.r=r1;
    s.v=s.v+(a0+a1)*(0.5*dt);
    s.mass=tmp.mass;
    s.time+=dt;
}

struct Event { double t; double radius; bool peri; };

Event next_apsis(State& s, bool peri) {
    double prev=radial_velocity(s);
    for (int i=0;i<400000;++i) {
        step(s,kDt,0.0);
        const double cur=radial_velocity(s);
        const bool is_peri=prev<0.0 && cur>=0.0;
        const bool is_apo=prev>0.0 && cur<=0.0;
        if ((peri && is_peri) || (!peri && is_apo)) return {s.time,s.r.magnitude(),peri};
        prev=cur;
    }
    throw std::runtime_error("apsis not found");
}

void burn_to_delta_v(State& s, double dv, double signed_accel) {
    if (dv <= 0.0) return;
    const double mdot=std::abs(kThrust)/(kIsp*kG0);
    const double vf=signed_accel > 0 ? 1.0 : -1.0;
    const double final_mass=s.mass*std::exp(-dv/(kIsp*kG0));
    const double duration=(s.mass-final_mass)/mdot;
    const int steps=std::max(1, static_cast<int>(std::ceil(duration/kDt)));
    const double dt=duration/static_cast<double>(steps);
    const double thrust = signed_accel * kThrust;
    (void)vf;
    for (int i=0;i<steps;++i) step(s,dt,thrust);
}

State make_initial() {
    const double rp=kMoonRadius+kInitialRpAlt, ra=kMoonRadius+kInitialRaAlt;
    const double a=0.5*(rp+ra);
    const double vp=std::sqrt(kMuMoon*(2.0/rp-1.0/a));
    return {{rp,0,0},{0,vp,0},kInitialMass,0.0};
}

} // namespace

int main() {
    try {
        trishula::LunarDescentPreparationPlanner planner;
        const auto plan=planner.plan(kMuMoon,kMoonRadius,kInitialRpAlt,kInitialRaAlt,kTargetRpAlt,kTargetRaAlt);
        if (!plan.valid || plan.total_delta_v_m_per_s <= 0.0) throw std::runtime_error("invalid descent preparation plan");

        State s=make_initial();
        const auto first_apo=next_apsis(s,false);
        const double mass_before1=s.mass;
        burn_to_delta_v(s,plan.first_burn_delta_v_m_per_s,-1.0);
        const auto transfer_peri=next_apsis(s,true);
        const double transfer_perilune_km=(transfer_peri.radius-kMoonRadius)/1000.0;

        const double mass_after1=s.mass;
        const double first_executed_dv=kIsp*kG0*std::log(kInitialMass/mass_after1);
        const double mass_before2=s.mass;
        const double target_a=0.5*((kMoonRadius+kTargetRpAlt)+(kMoonRadius+kTargetRaAlt));
        const double v_transfer=s.v.magnitude();
        const double v_target=std::sqrt(kMuMoon*(2.0/transfer_peri.radius-1.0/target_a));
        const double dv2=std::max(0.0,v_transfer-v_target);
        burn_to_delta_v(s,dv2,-1.0);
        const auto final_peri=next_apsis(s,true);
        const auto final_apo=next_apsis(s,false);

        const double final_rp_alt=(final_peri.radius-kMoonRadius)/1000.0;
        const double final_ra_alt=(final_apo.radius-kMoonRadius)/1000.0;
        const double consumed=kInitialMass-s.mass;
        const double dv2_exec=mass_before2>kDryMass ? kIsp*kG0*std::log(mass_before2/s.mass) : 0.0;
        const double total_exec_dv= kIsp*kG0*std::log(kInitialMass/s.mass);
        const bool target_ok=std::abs(final_rp_alt-kTargetRpAlt/1000.0)<2.0 && std::abs(final_ra_alt-kTargetRaAlt/1000.0)<5.0;
        const bool safe=final_rp_alt>5.0;
        const bool bound=sma(s)>kMoonRadius && ecc(s)<0.3;
        const bool mass_ok=s.mass>kDryMass && consumed>0.0;
        const bool pass=target_ok && safe && bound && mass_ok && transfer_perilune_km<kInitialRpAlt/1000.0;

        std::cout<<std::fixed<<std::setprecision(6);
        std::cout<<"TRISHULA V0.9.28 - Lunar Descent Preparation\n";
        std::cout<<"==============================================================\n";
        std::cout<<"  initial perilune altitude         : "<<kInitialRpAlt<<" m\n";
        std::cout<<"  initial apolune altitude          : "<<kInitialRaAlt<<" m\n";
        std::cout<<"  target landing-interface perilune : "<<kTargetRpAlt<<" m\n";
        std::cout<<"  target descent apolune             : "<<kTargetRaAlt<<" m\n";
        std::cout<<"  first burn planned dV             : "<<plan.first_burn_delta_v_m_per_s<<" m/s\n";
        std::cout<<"  first burn execution dV           : "<<first_executed_dv<<" m/s\n";
        std::cout<<"  transfer perilune                  : "<<transfer_perilune_km<<" km\n";
        std::cout<<"  second burn planned dV             : "<<dv2<<" m/s\n";
        std::cout<<"  second burn execution dV           : "<<dv2_exec<<" m/s\n";
        std::cout<<"  total executed propulsive dV      : "<<total_exec_dv<<" m/s\n";
        std::cout<<"  propellant consumed                : "<<consumed<<" kg\n";
        std::cout<<"  final perilune altitude             : "<<final_rp_alt<<" km\n";
        std::cout<<"  final apolune altitude              : "<<final_ra_alt<<" km\n";
        std::cout<<"  final semi-major axis               : "<<sma(s)/1000.0<<" km\n";
        std::cout<<"  final eccentricity                  : "<<ecc(s)<<"\n";
        std::cout<<"  landing interface established       : "<<(target_ok?"YES":"NO")<<"\n";
        std::cout<<"  lunar surface clearance             : "<<(safe?"SAFE":"UNSAFE")<<"\n";
        std::cout<<"  descent orbit remains bound         : "<<(bound?"YES":"NO")<<"\n";
        std::cout<<"  remaining mass above dry mass       : "<<(mass_ok?"YES":"NO")<<"\n";
        if(!pass) throw std::runtime_error("lunar descent preparation acceptance failed");
        std::cout<<"\nV0.9.28 lunar descent preparation campaign PASSED.\n";
        return 0;
    } catch(const std::exception& ex) {
        std::cerr<<"V0.9.28 campaign FAILED: "<<ex.what()<<"\n";
        return 1;
    }
}
