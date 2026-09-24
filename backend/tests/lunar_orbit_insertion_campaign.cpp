#include "trishula/maneuver/lunar_orbit_insertion.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

constexpr double kMuMoon = 4.9048695e12;
constexpr double kMoonRadius = 1.7374e6;
constexpr double kPeriluneAlt = 100.0e3;
constexpr double kApoluneAlt = 1000.0e3;
constexpr double kVinf = 787.785675;
constexpr double kThrust = 5.0e6;
constexpr double kIsp = 450.0;
constexpr double kInitialMass = 24859.219435;
constexpr double kDryMass = 20000.0;
constexpr double kDt = 0.25;
constexpr double kOrbitTolerance = 5.0e3;
constexpr double kEccTolerance = 5.0e-3;

trishula::Vector3 accel(double mu, const trishula::Vector3& r, double mass, const trishula::Vector3& thrust) {
    const double radius = r.magnitude();
    if (radius <= 1.0) throw std::runtime_error("Moon collision / zero radius");
    return r * (-mu / (radius*radius*radius)) + thrust / mass;
}

struct State { trishula::Vector3 r{}; trishula::Vector3 v{}; double mass{0.0}; double time{0.0}; };

void step(State& s, double dt, const trishula::Vector3& thrust_force) {
    const auto a0 = accel(kMuMoon, s.r, s.mass, thrust_force);
    const auto r1 = s.r + s.v * dt + a0 * (0.5*dt*dt);
    const double thrust_mag = thrust_force.magnitude();
    const double mdot = thrust_mag / (kIsp * 9.80665);
    const double consumed = std::min(std::max(0.0, s.mass-kDryMass), mdot*dt);
    State provisional=s;
    provisional.r=r1;
    provisional.v=s.v+a0*dt;
    provisional.mass=s.mass-consumed;
    const auto a1 = accel(kMuMoon, provisional.r, provisional.mass, thrust_force);
    s.r = r1;
    s.v = s.v + (a0+a1)*(0.5*dt);
    s.mass = provisional.mass;
    s.time += dt;
}

int main(){
 try {
    const trishula::LunarOrbitInsertionPlanner planner;
    const auto plan=planner.plan(kMuMoon,kMoonRadius,kPeriluneAlt,kApoluneAlt,kVinf,kThrust,kIsp,kInitialMass,kDryMass);

    State s{};
    const double rp=kMoonRadius+kPeriluneAlt;
    const double incoming_r = kMuMoon / (kVinf*kVinf); // diagnostic scale only
    (void)incoming_r;
    // Start on the incoming hyperbola at a representative pre-burn radius.
    const double start_radius=5.0e6;
    const double start_speed=std::sqrt(kVinf*kVinf + 2.0*kMuMoon/start_radius);
    const double desired_h = rp * plan.pre_burn_speed_m_per_s;
    const double tangential = desired_h/start_radius;
    const double radial_sq = std::max(0.0,start_speed*start_speed - tangential*tangential);
    s.r={start_radius,0.0,0.0};
    s.v={-std::sqrt(radial_sq),tangential,0.0};
    s.mass=kInitialMass;

    double min_r=s.r.magnitude();
    bool perilune=false;
    for(int i=0;i<400000;i++){
        const double radial=s.r.dot(s.v)/s.r.magnitude();
        if(s.time>5.0 && radial>=0.0){ perilune=true; break; }
        step(s,kDt,{});
        min_r=std::min(min_r,s.r.magnitude());
    }
    if(!perilune) throw std::runtime_error("Failed to reach perilune");

    const double pre_speed=s.v.magnitude();
    const double rp_before=s.r.magnitude();
    const auto retro=s.v.normalized()*(-kThrust);
    const double target_dv=plan.planned_delta_v_m_per_s;
    double remaining=plan.planned_burn_duration_seconds;
    std::size_t burn_steps=0;
    const double initial_burn_mass=s.mass;
    while(remaining>1e-9){
        const double dt=std::min(kDt,remaining);
        step(s,dt,retro);
        remaining-=dt;
        ++burn_steps;
    }
    const double achieved_dv=pre_speed-s.v.magnitude();
    const double burn_prop=initial_burn_mass-s.mass;
    const double post_energy=trishula::specific_orbital_energy(kMuMoon,s.r,s.v);
    const double a=trishula::semi_major_axis_from_energy(kMuMoon,post_energy);
    const double e=trishula::eccentricity_from_state(kMuMoon,s.r,s.v);
    const double rp_actual=a*(1.0-e);
    const double ra_actual=a*(1.0+e);
    const double rp_alt=rp_actual-kMoonRadius;
    const double ra_alt=ra_actual-kMoonRadius;

    const double burn_error=std::abs(achieved_dv-target_dv);
    const bool captured=post_energy<0.0 && a>rp && e<0.25 && burn_error<10.0 && std::abs(rp_alt-kPeriluneAlt)<kOrbitTolerance && std::abs(ra_alt-kApoluneAlt)<kOrbitTolerance;

    std::cout<<std::fixed<<std::setprecision(6);
    std::cout<<"TRISHULA V0.9.26 - Lunar Orbit Insertion\n";
    std::cout<<"==============================================================\n";
    std::cout<<"  target perilune altitude          : "<<kPeriluneAlt<<" m\n";
    std::cout<<"  target apolune altitude           : "<<kApoluneAlt<<" m\n";
    std::cout<<"  incoming lunar v_inf              : "<<kVinf<<" m/s\n";
    std::cout<<"  planned LOI delta-v               : "<<plan.planned_delta_v_m_per_s<<" m/s\n";
    std::cout<<"  planned burn duration             : "<<plan.planned_burn_duration_seconds<<" s\n";
    std::cout<<"  perilune radius before burn       : "<<rp_before/1000.0<<" km\n";
    std::cout<<"  perilune speed before burn        : "<<pre_speed<<" m/s\n";
    std::cout<<"  achieved LOI delta-v              : "<<achieved_dv<<" m/s\n";
    std::cout<<"  burn integration steps            : "<<burn_steps<<"\n";
    std::cout<<"  propellant consumed               : "<<burn_prop<<" kg\n";
    std::cout<<"  post-burn specific energy         : "<<post_energy<<" J/kg\n";
    std::cout<<"  captured semi-major axis          : "<<a/1000.0<<" km\n";
    std::cout<<"  captured eccentricity             : "<<e<<"\n";
    std::cout<<"  captured perilune altitude        : "<<rp_alt/1000.0<<" km\n";
    std::cout<<"  captured apolune altitude         : "<<ra_alt/1000.0<<" km\n";
    std::cout<<"  captured orbit                    : "<<(post_energy<0.0?"YES":"NO")<<"\n";
    if(!captured) throw std::runtime_error("LOI capture / target orbit acceptance failed");
    std::cout<<"\nV0.9.26 lunar orbit insertion campaign PASSED.\n";
    return 0;
 } catch(const std::exception& ex){ std::cerr<<"V0.9.26 campaign FAILED: "<<ex.what()<<"\n"; return 1; }
}

