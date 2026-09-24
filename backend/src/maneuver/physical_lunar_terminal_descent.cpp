#include "trishula/maneuver/physical_lunar_terminal_descent.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace trishula {
namespace { constexpr double kEpsilon=1e-9; }
PhysicalLunarTerminalDescentExecutor::PhysicalLunarTerminalDescentExecutor(PhysicalLunarTerminalDescentConfiguration c,EphemerisFunction moon_position,EphemerisFunction moon_velocity,double mu,double r):configuration_(c),moon_position_(std::move(moon_position)),moon_velocity_(std::move(moon_velocity)),moon_gravitational_parameter_m3_s2_(mu),moon_radius_meters_(r){
 if(c.terminal_start_altitude_meters<=c.terminal_target_altitude_meters||c.terminal_target_altitude_meters<0||c.control_step_seconds<=0||c.maximum_duration_seconds<=0||mu<=0||r<=0||!moon_position_||!moon_velocity_) throw std::invalid_argument("Invalid physical terminal-descent configuration"); }
Vector3 PhysicalLunarTerminalDescentExecutor::relative_position(const SimulationEngine&s)const{return s.spacecraft().state().position_meters-moon_position_(s.clock().time());}
Vector3 PhysicalLunarTerminalDescentExecutor::relative_velocity(const SimulationEngine&s)const{return s.spacecraft().state().velocity_m_per_s-moon_velocity_(s.clock().time());}
double PhysicalLunarTerminalDescentExecutor::altitude(const SimulationEngine&s)const{return relative_position(s).magnitude()-moon_radius_meters_;}
double PhysicalLunarTerminalDescentExecutor::radial_velocity(const SimulationEngine&s)const{auto r=relative_position(s);auto v=relative_velocity(s);if(r.magnitude()<=kEpsilon)throw std::runtime_error("Invalid lunar-relative radius");return r.dot(v)/r.magnitude();}
PhysicalLunarTerminalDescentResult PhysicalLunarTerminalDescentExecutor::execute(SimulationEngine& s)const{
 PhysicalLunarTerminalDescentResult r{}; r.initial_altitude_meters=altitude(s); r.initial_radial_velocity_m_per_s=radial_velocity(s);
 if(r.initial_altitude_meters>configuration_.terminal_start_altitude_meters||r.initial_altitude_meters<=configuration_.terminal_target_altitude_meters)throw std::runtime_error("Current physical state is outside the V0.9.47 terminal-descent window");
 r.valid_initial_state=true; const double fuel=std::max(0.0,s.spacecraft().state().mass_kg-s.main_engine().dry_mass_kg()); r.remaining_propellant_kg=fuel; r.propellant_budget_sufficient=fuel>configuration_.minimum_propellant_reserve_kg;
 if(!r.propellant_budget_sufficient)throw std::runtime_error("V0.9.47 terminal descent blocked: insufficient propellant reserve");
 r.terminal_descent_started=true; const double m0=s.spacecraft().state().mass_kg; double t=0;
 while(t<configuration_.maximum_duration_seconds){double h=altitude(s);if(h<=configuration_.terminal_target_altitude_meters)break;double vr=radial_velocity(s);double g=moon_gravitational_parameter_m3_s2_/std::pow(moon_radius_meters_+h,2.0);double hover=g*s.spacecraft().state().mass_kg/s.main_engine().maximum_thrust_newtons();double damping=std::max(0.0,(vr-configuration_.target_radial_velocity_m_per_s)*0.08);double throttle=std::clamp(hover+damping,0.0,configuration_.maximum_throttle);const Vector3 rvec=relative_position(s);const Vector3 radial=rvec.normalized();const Vector3 inertial_direction=radial;PropulsionCommand c{};c.main_engine_throttle=throttle;c.commanded_thrust_direction_body=s.spacecraft().state().attitude_body_to_inertial.inverse().rotate(inertial_direction);s.step_for_duration(c,configuration_.control_step_seconds);t+=configuration_.control_step_seconds;++r.physical_propagation_steps;}
 r.duration_seconds=t;r.final_altitude_meters=altitude(s);r.final_radial_velocity_m_per_s=radial_velocity(s);r.propellant_consumed_kg=m0-s.spacecraft().state().mass_kg;r.remaining_propellant_kg=std::max(0.0,s.spacecraft().state().mass_kg-s.main_engine().dry_mass_kg());r.terminal_descent_active=r.terminal_descent_started&&r.final_altitude_meters<=configuration_.terminal_start_altitude_meters&&r.final_altitude_meters>configuration_.terminal_target_altitude_meters-25.0&&r.propellant_consumed_kg>0;return r;
}
}
