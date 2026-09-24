cmake_minimum_required(VERSION 3.20)
project(trishula_simulation VERSION 0.9.107.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(TRISHULA_BUILD_TESTS "Build TRISHULA tests" ON)

add_library(trishula_core
    src/core/simulation_clock.cpp
    src/environment/celestial_body.cpp
    src/physics/gravity.cpp
    src/physics/perturbations.cpp
    src/physics/dynamics.cpp
    src/physics/euler_integrator.cpp
    src/physics/attitude_dynamics.cpp
    src/physics/attitude_integrator.cpp
    src/vehicle/spacecraft.cpp
    src/propulsion/propulsion.cpp
    src/sensors/sensors.cpp
    src/estimation_state_estimator.cpp
    src/navigation/navigation.cpp
    src/navigation/lunar_navigation.cpp
    src/navigation/terrain_relative_navigation.cpp
    src/guidance/guidance.cpp
    src/guidance/lunar_powered_descent.cpp
    src/guidance/terrain_relative_powered_descent.cpp
    src/landing/lunar_terrain.cpp
    src/landing/lander_contact_dynamics.cpp
    src/landing/physical_lunar_landing_integration.cpp
    src/surface/post_landing_operations.cpp
    src/rover/surface_rover.cpp
    src/rover/autonomous_rover_navigation.cpp
    src/rover/closed_loop_rover_navigation.cpp
    src/rover/multi_objective_mission_planner.cpp
    src/rover/dynamic_multi_objective_mission_planner.cpp
    src/rover/science_operations.cpp
    src/rover/science_driven_mission_planner.cpp
    src/rover/science_data_products.cpp
    src/rover/science_knowledge.cpp
    src/rover/science_archive.cpp
    src/rover/science_relay.cpp
    src/rover/ground_science_processing.cpp
    src/rover/autonomous_surface_mission.cpp
    src/rover/science_instruments.cpp
    src/control/control.cpp
    src/maneuver/orbital_maneuver.cpp
    src/maneuver/trans_lunar_injection.cpp
    src/maneuver/lunar_orbit_insertion.cpp
    src/maneuver/lunar_descent_prep.cpp
    src/maneuver/physical_lunar_powered_descent.cpp
    src/maneuver/physical_lunar_terminal_descent.cpp
    src/maneuver/finite_burn_executor.cpp
    src/maneuver/finite_burn_predictor.cpp
    src/maneuver/recovery_maneuver_executor.cpp
    src/maneuver/maneuver_performance.cpp
    src/physics/velocity_verlet_integrator.cpp
    src/core/simulation_engine.cpp
    src/ground/space_link.cpp
    src/ground/telemetry_ingestion.cpp
    src/ground/event_ingestion.cpp
    src/ground/command_ingestion.cpp
    src/ground/command_execution.cpp
    src/ground/vehicle_command_adapter.cpp
    src/ground/spacecraft_command_link.cpp
    src/ground/spacecraft_command_round_trip.cpp
    src/ground/closed_loop_feedback.cpp
    src/ground/closed_loop_mission_cycle.cpp
    src/ground/mission_data_model.cpp
    src/mission/mission_execution.cpp
    src/mission/physical_mission_execution.cpp
    src/mission/physical_mission_telemetry_loop.cpp
    src/mission/physical_mission_phase_controller.cpp
    src/mission/physical_mission_runtime_controller.cpp
    src/mission/physical_mission_ground_uplink.cpp
)

target_include_directories(trishula_core PUBLIC include)

target_compile_options(trishula_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Werror>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

add_executable(trishula_demo src/main.cpp)
target_link_libraries(trishula_demo PRIVATE trishula_core)

add_executable(trishula_ground_command_bridge src/ground/vehicle_command_bridge.cpp)
target_link_libraries(trishula_ground_command_bridge PRIVATE trishula_core)

add_executable(trishula_physical_mission_ground_uplink tests/physical_mission_ground_uplink_main.cpp)
target_link_libraries(trishula_physical_mission_ground_uplink PRIVATE trishula_core)
if(WIN32)
    target_link_libraries(trishula_physical_mission_ground_uplink PRIVATE ws2_32)
endif()

if(TRISHULA_BUILD_TESTS)
    enable_testing()
    add_executable(trishula_tests tests/test_main.cpp)
    target_link_libraries(trishula_tests PRIVATE trishula_core)
    add_test(NAME trishula_tests COMMAND trishula_tests)
    add_executable(trishula_perturbation_targeting tests/perturbation_targeting_campaign.cpp)
    target_link_libraries(trishula_perturbation_targeting PRIVATE trishula_core)
    add_test(NAME trishula_perturbation_targeting COMMAND trishula_perturbation_targeting)
    add_executable(trishula_tli_campaign tests/tli_campaign.cpp)
    target_link_libraries(trishula_tli_campaign PRIVATE trishula_core)
    add_test(NAME trishula_tli_campaign COMMAND trishula_tli_campaign)
    add_executable(trishula_tli_execution tests/tli_execution_campaign.cpp)
    target_link_libraries(trishula_tli_execution PRIVATE trishula_core)
    add_test(NAME trishula_tli_execution COMMAND trishula_tli_execution)
    add_executable(trishula_lunar_approach tests/lunar_approach_campaign.cpp)
    target_link_libraries(trishula_lunar_approach PRIVATE trishula_core)
    add_test(NAME trishula_lunar_approach COMMAND trishula_lunar_approach)
    add_executable(trishula_lunar_orbit_insertion tests/lunar_orbit_insertion_campaign.cpp)
    target_link_libraries(trishula_lunar_orbit_insertion PRIVATE trishula_core)
    add_test(NAME trishula_lunar_orbit_insertion COMMAND trishula_lunar_orbit_insertion)
    add_executable(trishula_lunar_orbit_operations tests/lunar_orbit_operations_campaign.cpp)
    target_link_libraries(trishula_lunar_orbit_operations PRIVATE trishula_core)
    add_test(NAME trishula_lunar_orbit_operations COMMAND trishula_lunar_orbit_operations)
    add_executable(trishula_lunar_descent_preparation tests/lunar_descent_preparation_campaign.cpp)
    target_link_libraries(trishula_lunar_descent_preparation PRIVATE trishula_core)
    add_test(NAME trishula_lunar_descent_preparation COMMAND trishula_lunar_descent_preparation)

    add_executable(trishula_lunar_powered_descent tests/lunar_powered_descent_campaign.cpp)
    target_link_libraries(trishula_lunar_powered_descent PRIVATE trishula_core)
    add_test(NAME trishula_lunar_powered_descent COMMAND trishula_lunar_powered_descent)

    add_executable(trishula_landing_hazard tests/lunar_landing_hazard_campaign.cpp)
    target_link_libraries(trishula_landing_hazard PRIVATE trishula_core)
    add_test(NAME trishula_landing_hazard COMMAND trishula_landing_hazard)

    add_executable(trishula_terrain_relative_navigation tests/terrain_relative_navigation_campaign.cpp)
    target_link_libraries(trishula_terrain_relative_navigation PRIVATE trishula_core)
    add_test(NAME trishula_terrain_relative_navigation COMMAND trishula_terrain_relative_navigation)

    add_executable(trishula_autonomous_landing tests/autonomous_landing_campaign.cpp)
    add_executable(trishula_lander_contact_dynamics tests/lander_contact_dynamics_campaign.cpp)
    target_link_libraries(trishula_lander_contact_dynamics PRIVATE trishula_core)
    add_test(NAME trishula_lander_contact_dynamics COMMAND trishula_lander_contact_dynamics)
    add_executable(trishula_post_landing_operations tests/post_landing_operations_campaign.cpp)
    target_link_libraries(trishula_post_landing_operations PRIVATE trishula_core)
    add_test(NAME trishula_post_landing_operations COMMAND trishula_post_landing_operations)
    add_executable(trishula_mission_execution tests/mission_execution_campaign.cpp)
    target_link_libraries(trishula_mission_execution PRIVATE trishula_core)
    add_test(NAME trishula_mission_execution COMMAND trishula_mission_execution)
    add_executable(trishula_physical_mission_execution tests/physical_mission_execution_campaign.cpp)
    target_link_libraries(trishula_physical_mission_execution PRIVATE trishula_core)
    add_test(NAME trishula_physical_mission_execution COMMAND trishula_physical_mission_execution)
    add_executable(trishula_physical_mission_telemetry_loop tests/physical_mission_telemetry_loop_campaign.cpp)
    target_link_libraries(trishula_physical_mission_telemetry_loop PRIVATE trishula_core)
    add_test(NAME trishula_physical_mission_telemetry_loop COMMAND trishula_physical_mission_telemetry_loop)
    add_executable(trishula_physical_mission_runtime_controller tests/physical_mission_runtime_controller_campaign.cpp)
    target_link_libraries(trishula_physical_mission_runtime_controller PRIVATE trishula_core)
    add_test(NAME trishula_physical_mission_runtime_controller COMMAND trishula_physical_mission_runtime_controller)
    add_executable(trishula_physical_mission_runtime_bridge tests/physical_mission_runtime_bridge_main.cpp)
    target_link_libraries(trishula_physical_mission_runtime_bridge PRIVATE trishula_core)
    add_executable(trishula_physical_mission_phase_controller tests/physical_mission_phase_controller_campaign.cpp)
    target_link_libraries(trishula_physical_mission_phase_controller PRIVATE trishula_core)
    add_test(NAME trishula_physical_mission_phase_controller COMMAND trishula_physical_mission_phase_controller)
    add_executable(trishula_physical_mission_ground_uplink_campaign tests/physical_mission_ground_uplink_campaign.cpp)
    target_link_libraries(trishula_physical_mission_ground_uplink_campaign PRIVATE trishula_core)
    if(WIN32)
        target_link_libraries(trishula_physical_mission_ground_uplink_campaign PRIVATE ws2_32)
    endif()
    add_test(NAME trishula_physical_mission_ground_uplink_campaign COMMAND trishula_physical_mission_ground_uplink_campaign)
    add_executable(trishula_physical_earth_moon_arc tests/physical_earth_moon_arc_campaign.cpp)
    target_link_libraries(trishula_physical_earth_moon_arc PRIVATE trishula_core)
    add_test(NAME trishula_physical_earth_moon_arc COMMAND trishula_physical_earth_moon_arc)
    add_executable(trishula_physical_integrated_earth_moon_loi tests/physical_integrated_earth_moon_loi_campaign.cpp)
    target_link_libraries(trishula_physical_integrated_earth_moon_loi PRIVATE trishula_core)
    add_test(NAME trishula_physical_integrated_earth_moon_loi COMMAND trishula_physical_integrated_earth_moon_loi)
    add_executable(trishula_physical_lunar_orbit_operations tests/physical_lunar_orbit_operations_campaign.cpp)
    target_link_libraries(trishula_physical_lunar_orbit_operations PRIVATE trishula_core)
    add_test(NAME trishula_physical_lunar_orbit_operations COMMAND trishula_physical_lunar_orbit_operations)
    add_executable(trishula_physical_lunar_descent_preparation tests/physical_lunar_descent_preparation_campaign.cpp)
    target_link_libraries(trishula_physical_lunar_descent_preparation PRIVATE trishula_core)
    add_test(NAME trishula_physical_lunar_descent_preparation COMMAND trishula_physical_lunar_descent_preparation)
    add_executable(trishula_physical_lunar_powered_descent tests/physical_lunar_powered_descent_campaign.cpp)
    target_link_libraries(trishula_physical_lunar_powered_descent PRIVATE trishula_core)
    add_test(NAME trishula_physical_lunar_powered_descent COMMAND trishula_physical_lunar_powered_descent)
    add_executable(trishula_rover_deployment_mobility tests/rover_deployment_mobility_campaign.cpp)
    target_link_libraries(trishula_rover_deployment_mobility PRIVATE trishula_core)
    add_test(NAME trishula_rover_deployment_mobility COMMAND trishula_rover_deployment_mobility)
    add_executable(trishula_rover_autonomous_navigation tests/rover_autonomous_navigation_campaign.cpp)
    target_link_libraries(trishula_rover_autonomous_navigation PRIVATE trishula_core)
    add_test(NAME trishula_rover_autonomous_navigation COMMAND trishula_rover_autonomous_navigation)
    add_executable(trishula_rover_closed_loop_navigation tests/rover_closed_loop_navigation_campaign.cpp)
    target_link_libraries(trishula_rover_closed_loop_navigation PRIVATE trishula_core)
    add_test(NAME trishula_rover_closed_loop_navigation COMMAND trishula_rover_closed_loop_navigation)
    add_executable(trishula_rover_dynamic_obstacle tests/rover_dynamic_obstacle_campaign.cpp)
    target_link_libraries(trishula_rover_dynamic_obstacle PRIVATE trishula_core)
    add_test(NAME trishula_rover_dynamic_obstacle COMMAND trishula_rover_dynamic_obstacle)
    add_executable(trishula_rover_multi_objective tests/rover_multi_objective_mission_campaign.cpp)
    target_link_libraries(trishula_rover_multi_objective PRIVATE trishula_core)
    add_test(NAME trishula_rover_multi_objective COMMAND trishula_rover_multi_objective)
    add_executable(trishula_rover_dynamic_multi_objective tests/rover_dynamic_multi_objective_mission_campaign.cpp)
    target_link_libraries(trishula_rover_dynamic_multi_objective PRIVATE trishula_core)
    add_test(NAME trishula_rover_dynamic_multi_objective COMMAND trishula_rover_dynamic_multi_objective)
    add_executable(trishula_rover_science_operations tests/rover_science_operations_campaign.cpp)
    target_link_libraries(trishula_rover_science_operations PRIVATE trishula_core)
    add_test(NAME trishula_rover_science_operations COMMAND trishula_rover_science_operations)
    add_executable(trishula_rover_science_driven_mission tests/rover_science_driven_mission_campaign.cpp)
    target_link_libraries(trishula_rover_science_driven_mission PRIVATE trishula_core)
    add_test(NAME trishula_rover_science_driven_mission COMMAND trishula_rover_science_driven_mission)
    add_executable(trishula_rover_science_knowledge tests/rover_science_knowledge_campaign.cpp)
    target_link_libraries(trishula_rover_science_knowledge PRIVATE trishula_core)
    add_test(NAME trishula_rover_science_knowledge COMMAND trishula_rover_science_knowledge)
    add_executable(trishula_rover_science_persistent_storage tests/rover_science_persistent_storage_campaign.cpp)
    target_link_libraries(trishula_rover_science_persistent_storage PRIVATE trishula_core)
    add_test(NAME trishula_rover_science_persistent_storage COMMAND trishula_rover_science_persistent_storage)
    add_executable(trishula_rover_science_relay tests/rover_science_relay_campaign.cpp)
    target_link_libraries(trishula_rover_science_relay PRIVATE trishula_core)
    add_test(NAME trishula_rover_science_relay COMMAND trishula_rover_science_relay)
    add_executable(trishula_rover_science_instruments tests/rover_science_instrument_campaign.cpp)
    target_link_libraries(trishula_rover_science_instruments PRIVATE trishula_core)
    add_test(NAME trishula_rover_science_instruments COMMAND trishula_rover_science_instruments)
    add_executable(trishula_rover_physical_science_pipeline tests/rover_physical_science_pipeline_campaign.cpp)
    target_link_libraries(trishula_rover_physical_science_pipeline PRIVATE trishula_core)
    add_test(NAME trishula_rover_physical_science_pipeline COMMAND trishula_rover_physical_science_pipeline)
    add_executable(trishula_ground_science_processing tests/ground_science_processing_campaign.cpp)
    target_link_libraries(trishula_ground_science_processing PRIVATE trishula_core)
    add_test(NAME trishula_ground_science_processing COMMAND trishula_ground_science_processing)
    add_executable(trishula_autonomous_surface_mission tests/autonomous_surface_mission_campaign.cpp)
    target_link_libraries(trishula_autonomous_surface_mission PRIVATE trishula_core)
    add_test(NAME trishula_autonomous_surface_mission COMMAND trishula_autonomous_surface_mission)
    add_executable(trishula_ground_station_packet tests/ground_station_packet_campaign.cpp)
    target_link_libraries(trishula_ground_station_packet PRIVATE trishula_core)
    add_test(NAME trishula_ground_station_packet COMMAND trishula_ground_station_packet)
    add_executable(trishula_ground_telemetry_ingestion tests/ground_telemetry_ingestion_campaign.cpp)
    target_link_libraries(trishula_ground_telemetry_ingestion PRIVATE trishula_core)
    add_test(NAME trishula_ground_telemetry_ingestion COMMAND trishula_ground_telemetry_ingestion)
    add_executable(trishula_ground_event_ingestion tests/ground_event_ingestion_campaign.cpp)
    target_link_libraries(trishula_ground_event_ingestion PRIVATE trishula_core)
    add_test(NAME trishula_ground_event_ingestion COMMAND trishula_ground_event_ingestion)
    add_executable(trishula_ground_command_ingestion tests/ground_command_ingestion_campaign.cpp)
    target_link_libraries(trishula_ground_command_ingestion PRIVATE trishula_core)
    add_test(NAME trishula_ground_command_ingestion COMMAND trishula_ground_command_ingestion)
    add_executable(trishula_ground_command_ingestion_contract tests/ground_command_ingestion_contract_campaign.cpp)
    target_link_libraries(trishula_ground_command_ingestion_contract PRIVATE trishula_core)
    add_test(NAME trishula_ground_command_ingestion_contract COMMAND trishula_ground_command_ingestion_contract)
    add_executable(trishula_dsa_ground_ingestion_queue tests/dsa_ground_ingestion_queue_campaign.cpp)
    target_link_libraries(trishula_dsa_ground_ingestion_queue PRIVATE trishula_core)
    add_test(NAME trishula_dsa_ground_ingestion_queue COMMAND trishula_dsa_ground_ingestion_queue)
    add_executable(trishula_dsa_ground_ingestion_queue_benchmark tests/dsa_ground_ingestion_queue_benchmark.cpp)
    target_link_libraries(trishula_dsa_ground_ingestion_queue_benchmark PRIVATE trishula_core)
    add_executable(trishula_ground_ingestion_queue_pressure tests/ground_ingestion_queue_pressure_campaign.cpp)
    target_link_libraries(trishula_ground_ingestion_queue_pressure PRIVATE trishula_core)
    add_test(NAME trishula_ground_ingestion_queue_pressure COMMAND trishula_ground_ingestion_queue_pressure)
    add_executable(trishula_ground_command_execution tests/ground_command_execution_campaign.cpp)
    target_link_libraries(trishula_ground_command_execution PRIVATE trishula_core)
    add_test(NAME trishula_ground_command_execution COMMAND trishula_ground_command_execution)
    add_executable(trishula_ground_command_acknowledgement tests/ground_command_acknowledgement_campaign.cpp)
    target_link_libraries(trishula_ground_command_acknowledgement PRIVATE trishula_core)
    add_test(NAME trishula_ground_command_acknowledgement COMMAND trishula_ground_command_acknowledgement)
    add_executable(trishula_ground_vehicle_command_adapter tests/ground_vehicle_command_adapter_campaign.cpp)
    target_link_libraries(trishula_ground_vehicle_command_adapter PRIVATE trishula_core)
    add_test(NAME trishula_ground_vehicle_command_adapter COMMAND trishula_ground_vehicle_command_adapter)
    add_executable(trishula_ground_vehicle_command_adapter_safety tests/ground_vehicle_command_adapter_safety_campaign.cpp)
    target_link_libraries(trishula_ground_vehicle_command_adapter_safety PRIVATE trishula_core)
    add_test(NAME trishula_ground_vehicle_command_adapter_safety COMMAND trishula_ground_vehicle_command_adapter_safety)
    target_link_libraries(trishula_autonomous_landing PRIVATE trishula_core)


    add_executable(trishula_ground_spacecraft_command_link tests/ground_spacecraft_command_link_campaign.cpp)
    target_link_libraries(trishula_ground_spacecraft_command_link PRIVATE trishula_core)
    add_test(NAME trishula_ground_spacecraft_command_link COMMAND trishula_ground_spacecraft_command_link)
    add_executable(trishula_ground_spacecraft_command_round_trip tests/ground_spacecraft_command_round_trip_campaign.cpp)
    target_link_libraries(trishula_ground_spacecraft_command_round_trip PRIVATE trishula_core)
    add_test(NAME trishula_ground_spacecraft_command_round_trip COMMAND trishula_ground_spacecraft_command_round_trip)
    add_executable(trishula_ground_closed_loop_feedback tests/ground_closed_loop_feedback_campaign.cpp)
    target_link_libraries(trishula_ground_closed_loop_feedback PRIVATE trishula_core)
    add_test(NAME trishula_ground_closed_loop_feedback COMMAND trishula_ground_closed_loop_feedback)
    add_executable(trishula_ground_closed_loop_mission_cycle tests/ground_closed_loop_mission_cycle_campaign.cpp)
    target_link_libraries(trishula_ground_closed_loop_mission_cycle PRIVATE trishula_core)
    add_test(NAME trishula_ground_closed_loop_mission_cycle COMMAND trishula_ground_closed_loop_mission_cycle)
    add_executable(trishula_ground_mission_data_model tests/ground_mission_data_model_campaign.cpp)
    target_link_libraries(trishula_ground_mission_data_model PRIVATE trishula_core)
    add_test(NAME trishula_ground_mission_data_model COMMAND trishula_ground_mission_data_model)
    add_test(NAME trishula_autonomous_landing COMMAND trishula_autonomous_landing)
endif()

add_executable(trishula_physical_lunar_terminal_descent tests/physical_lunar_terminal_descent_campaign.cpp)
target_link_libraries(trishula_physical_lunar_terminal_descent PRIVATE trishula_core)
add_test(NAME trishula_physical_lunar_terminal_descent COMMAND trishula_physical_lunar_terminal_descent)

add_executable(trishula_physical_lunar_landing_integration tests/physical_lunar_landing_integration_campaign.cpp)
target_link_libraries(trishula_physical_lunar_landing_integration PRIVATE trishula_core)
add_test(NAME trishula_physical_lunar_landing_integration COMMAND trishula_physical_lunar_landing_integration)
