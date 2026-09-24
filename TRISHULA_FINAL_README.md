# TRISHULA

## Closed-Loop Software-Based Physical Lunar Mission Simulation and Mission Control Platform

TRISHULA is a software-based physical lunar mission simulation and mission-control platform. It validates how spacecraft physics, mission states, telemetry, ground systems, mission control, commands, rover autonomy, and science operations work together as one closed-loop system.

> **Important:** TRISHULA is a software simulation and engineering platform. It is not flight-qualified spacecraft software, real spacecraft hardware, an official ISRO system, or a replacement for real mission qualification.

## 1. Project Goal

The core TRISHULA rule is:

```text
Physical State
    ↓
Physical Computation
    ↓
Physical Action
    ↓
New Physical State
    ↓
Telemetry / Mission Record
    ↓
Ground Data Service
    ↓
Ground Processing
    ↓
Mission Control
    ↓
Command
    ↓
Physical Command Execution
    ↓
New Physical State
```

The project avoids a UI simply inventing telemetry or moving a mission forward. Modelled physical state drives the mission chain.

## 2. High-Level Architecture

```text
                    TRISHULA
                       |
          +------------+------------+
          |                         |
   Physical / Vehicle Side     Ground Side
          |                         |
        C++                 Go Ground Services
          |                         |
          +------------+------------+
                       |
                 Mission Control
                       |
                 Next.js / React
```

### Physical / Vehicle Side — C++

The C++ side contains modelled:

- physics, gravity, perturbations and dynamics
- numerical integration and attitude dynamics
- spacecraft and propulsion
- sensors and state estimation
- navigation and lunar navigation
- terrain-relative navigation
- guidance, powered descent and landing dynamics
- rover systems and autonomous navigation
- science
- physical mission execution
- runtime control
- ground uplink

### Ground Side — Go

The Go side contains:

- telemetry, event and command ingestion
- command validation and execution
- command acknowledgement
- vehicle command adapter
- spacecraft command link
- command round-trip
- mission data model
- ground processing
- durable mission history
- current state
- REST APIs

### Mission Control — Next.js / React / TypeScript

Mission Control provides the user-facing mission-control interface and displays authoritative modelled/backend data.

## 3. Technology Stack

```text
C++, Go, Next.js, React, TypeScript
Kafka, PostgreSQL, Redis
REST APIs
Docker, Docker Compose
Jenkins, CMake, CTest
Git, GitHub
A*, Autonomous Navigation
Mission Control, Telemetry
Science Data Processing
```

## 4. Complete Mission Flow

```text
Earth
  ↓
TLI Campaign
  ↓
TLI Execution
  ↓
Lunar Approach
  ↓
Lunar Orbit Insertion
  ↓
Lunar Orbit Operations
  ↓
Descent Preparation
  ↓
Powered Descent
  ↓
Terrain Relative Navigation
  ↓
Landing
  ↓
Post-Landing Operations
  ↓
Lander / Vikram
  ↓
Rover Deployment
  ↓
Surface Mobility
  ↓
Localization
  ↓
A* Autonomous Navigation
  ↓
Hazard Handling
  ↓
Science Operations
  ↓
LIBS / APXS / Camera
  ↓
Science Pipeline
  ↓
Data Products
  ↓
Persistent Storage
  ↓
Rover → Vikram → Ground
  ↓
Ground Science Processing
  ↓
Mission Knowledge
  ↓
Mission Control
```

## 5. Spacecraft and Mission Simulation

TRISHULA models physical mission behaviour in software.

### Physics

- gravity
- perturbations
- spacecraft dynamics
- numerical integration
- attitude dynamics

### Propulsion

Propulsion behaviour participates in mission execution and trajectory phases.

### Sensors and Estimation

Modelled sensors provide information for state estimation and navigation.

### Navigation

The project includes spacecraft navigation, lunar navigation and terrain-relative navigation.

### Guidance and Control

Guidance and control support Earth-to-Moon transfer, lunar approach, lunar orbit insertion, descent, powered descent and landing.

## 6. Earth-to-Moon and Landing Mission

```text
Earth
 ↓
TLI
 ↓
Earth → Moon Transfer
 ↓
Lunar Approach
 ↓
Lunar Orbit Insertion
 ↓
Lunar Orbit Operations
 ↓
Descent Preparation
 ↓
Powered Descent
 ↓
Terrain Relative Navigation
 ↓
Landing
 ↓
Post-Landing Operations
```

Landing-related areas include landing hazards, terrain-relative navigation, autonomous landing, lander contact dynamics, terminal descent, post-landing operations and integrated lunar landing.

## 7. Rover Subsystem

The rover is a modelled operational subsystem, not only a visual UI object.

It includes:

- deployment
- surface mobility
- localization
- autonomous navigation
- closed-loop navigation
- dynamic obstacle handling
- multi-objective planning
- dynamic multi-objective planning
- autonomous surface mission operations

Navigation:

```text
Rover State
    ↓
Terrain / Environment
    ↓
A* Path Planning
    ↓
Navigation
    ↓
Hazard Handling
    ↓
Movement
    ↓
New Rover State
    ↓
Re-planning
```

## 8. Rover Science

Simulated instruments:

- LIBS
- APXS
- Camera

Science flow:

```text
Observation
    ↓
Instrument Output
    ↓
Calibration
    ↓
Data Product Generation
    ↓
Persistent Storage
    ↓
Integrity / Checksum Validation
    ↓
Rover Relay
    ↓
Vikram
    ↓
Ground
    ↓
Ground Science Processing
    ↓
Mission Knowledge
```

The project includes science operations, science-driven mission behaviour, science knowledge, persistent storage, relay, instruments, physical science processing and ground science processing.

## 9. Rover → Vikram → Ground Relay

```text
Rover
  ↓
Vikram / Lander
  ↓
Ground
  ↓
Ground Processing
  ↓
Mission Control
```

This is software modelling, not a real RF or spacecraft radio implementation.

## 10. Ground Data Service

The Ground Data Service is implemented in Go. It receives and processes mission records and supports telemetry, events, commands, acknowledgements, mission records, science-related ground data, ground processing and Mission Control REST APIs.

## 11. Kafka, PostgreSQL and Redis

Kafka provides mission-record streaming. The ground architecture includes:

```text
trishula.ground.records
```

PostgreSQL provides durable mission history.

Redis provides current mission state.

```text
PostgreSQL → durable history
Redis      → current state
```

## 12. Telemetry and Ground Flow

```text
Physical State
      ↓
Physical Mission Execution
      ↓
Telemetry Loop
      ↓
Ground Uplink
      ↓
Ground Telemetry Ingestion
      ↓
Processing
      ↓
PostgreSQL / Redis
      ↓
Mission Control
```

Events follow the ground event-ingestion path.

## 13. Command System

```text
Mission Control
      ↓
REST API
      ↓
Command Ingestion
      ↓
Contract Validation
      ↓
Command Execution
      ↓
Vehicle Command Adapter
      ↓
Spacecraft Command Link
      ↓
Physical Simulation
      ↓
New Physical State
      ↓
Telemetry
      ↓
Ground
      ↓
Mission Control
```

Validated command areas include command ingestion, command contract, execution, acknowledgement, vehicle command adapter, adapter safety, spacecraft command link, spacecraft command round-trip, closed-loop feedback and closed-loop mission cycle.

## 14. Mission Control

Mission Control is built with Next.js, React and TypeScript.

It provides mission-control views for mission state, spacecraft state, rover state, science information, ground information, telemetry, events, commands and mission records.

The UI must display authoritative backend/modelled data rather than invent mission values.

## 15. Runtime Control

The runtime layer includes:

- physical mission runtime controller
- runtime bridge
- mission phase controller
- physical mission execution
- physical mission telemetry loop
- physical mission ground uplink campaign

## 16. Closed-Loop Architecture

```text
                 +----------------------+
                 |                      |
                 v                      |
        Physical Simulation            |
                 |                      |
                 v                      |
             Telemetry                 |
                 |                      |
                 v                      |
        Ground Data Service            |
                 |                      |
        +--------+--------+             |
        |        |        |             |
        v        v        v             |
     Kafka   PostgreSQL  Redis          |
        |        |        |             |
        +--------+--------+             |
                 |                      |
                 v                      |
          Mission Control               |
                 |                      |
                 v                      |
              Command -----------------+
```

## 17. End-to-End Validation

TRISHULA has validation for:

- physical telemetry
- physical mission execution
- ground uplink
- command round-trip
- closed-loop feedback
- closed-loop mission cycle
- rover autonomy
- science operations
- science relay
- ground science processing
- mission knowledge

The current C++ CTest suite contains **58 tests**.

### Main C++ coverage

**Mission and trajectory**

- core tests
- perturbation targeting
- TLI campaign and execution
- lunar approach
- lunar orbit insertion and operations
- descent preparation and powered descent
- landing hazard
- terrain-relative navigation
- lander contact dynamics
- post-landing operations
- mission execution

**Physical mission**

- physical mission execution
- telemetry loop
- runtime controller
- phase controller
- ground uplink campaign
- physical Earth-Moon arc
- integrated Earth-Moon LOI
- physical lunar orbit operations
- physical descent preparation
- physical powered descent
- physical terminal descent
- physical landing integration

**Rover and science**

- deployment/mobility
- autonomous and closed-loop navigation
- dynamic obstacles
- multi-objective and dynamic multi-objective planning
- science operations
- science-driven mission
- science knowledge
- persistent storage
- science relay
- science instruments
- physical science pipeline
- autonomous surface mission
- ground science processing

**Ground and command**

- ground station packet
- telemetry ingestion
- event ingestion
- command ingestion and contract
- DSA ground ingestion queue
- queue pressure
- command execution
- acknowledgement
- vehicle command adapter and safety
- spacecraft command link
- command round-trip
- closed-loop feedback
- closed-loop mission cycle
- ground mission data model

**Landing**

- autonomous landing
- terminal descent
- integrated lunar landing

## 18. DSA / Queue Work

Ground ingestion includes:

- DSA ground ingestion queue
- queue-pressure validation
- queue benchmark coverage

## 19. Jenkins Continuous Integration

TRISHULA uses **Jenkins for Continuous Integration**.

Job:

```text
TRISHULA-CI
```

Repository:

```text
https://github.com/Debbatisudheer/TRISHULA.git
```

Branch:

```text
trishula-development
```

CI flow:

```text
GitHub
   ↓
Checkout
   ↓
Toolchain Preflight
   ↓
CMake Configure
   ↓
C++ Build
   ↓
C++ CTest
   ↓
Go Tests
   ↓
Go Vet
   ↓
npm ci
   ↓
TypeScript
   ↓
Next.js Production Build
   ↓
CI Artifacts
   ↓
SUCCESS
```

### Current Jenkins validation

The current Jenkins baseline successfully passed:

- Git checkout
- UCRT64 g++
- CMake
- CTest
- Go
- Node.js
- npm
- CMake configure
- C++ build
- **58/58 C++ tests**
- Go tests
- Go vet
- npm dependency installation
- TypeScript validation
- Next.js production build
- CI artifact packaging

The latest CI run finished successfully.

## 20. Jenkins Environment

```text
Jenkins    2.528.3
Java       17.0.17
Go         1.25.4
Node.js    24.11.1
npm        11.6.2
Docker     29.5.3
g++        16.2.0
CMake      4.4.3
CTest      4.4.3
```

C++ toolchain:

```text
MSYS2 UCRT64
C:\msys64
C:\msys64\ucrt64\bin
```

Jenkins node label:

```text
trishula-windows-ucrt64
```

## 21. Local Development

### Backend

```powershell
cd backend
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

Jenkins uses a separate `build_jenkins` directory.

### Go Ground Data Service

```powershell
cd backend\services\ground-data-service
go test ./...
go vet ./...
```

### Mission Control

```powershell
cd mission-control-ui
npm ci
npx tsc --noEmit
npm run build
```

Development:

```powershell
npm run dev
```

The Mission Control UI has been run locally on port `3001`.

## 22. Docker

Docker and Docker Compose are part of local supporting infrastructure. They can be used for services such as database infrastructure.

Docker/Kubernetes/cloud production deployment is not currently part of the project scope.

## 23. Repository Structure

```text
TRISHULA/
│
├── backend/
│   ├── CMakeLists.txt
│   ├── C++ source
│   ├── headers
│   ├── tests
│   └── services/
│       └── ground-data-service/
│
├── mission-control-ui/
│   ├── app/
│   ├── public/
│   ├── package.json
│   ├── package-lock.json
│   └── Next.js configuration
│
├── Jenkinsfile
├── .gitignore
└── README.md
```

Generated directories such as `backend/build/`, `backend/build_jenkins/`, `mission-control-ui/node_modules/` and `mission-control-ui/.next/` are excluded from Git.

## 24. Git

Repository:

```text
https://github.com/Debbatisudheer/TRISHULA.git
```

Branches:

```text
master
trishula-development
```

Active development branch:

```text
trishula-development
```

Clone:

```bash
git clone -b trishula-development https://github.com/Debbatisudheer/TRISHULA.git
cd TRISHULA
```

## 25. Version and Major Checkpoints

TRISHULA was developed incrementally rather than restarted.

The architecture-study phase covered **Levels 1 through 25** and is complete.

Important checkpoints include:

```text
V0.9.82
Command State Reconciliation

V0.9.83
Command Execution Orchestrator

V0.9.84
Command Orchestrator / Ground Data Service runtime

V0.9.97
Ground API version checkpoint

V0.9.105.1
Physical mission / ground integration progression

V0.9.110.1
Mission / physical integration progression

V0.9.112.1
Runtime control

V0.9.112.2
Runtime control validation

V0.9.123.1
Mission Control UI navigation / UI progression

V0.9.124.1
Jenkins CI baseline
```

The Mission Control UI has a `V0.4.x` development line; the current package reports UI version `0.4.12`.

These are major checkpoints; the project also contains incremental versions between them.

## 26. Architecture Study

The architecture-study phase was intentionally limited to:

```text
Level 1 → Level 25
```

After Level 25, the project moved into implementation and validation.

## 27. Security Scope

The previously considered `V0.9.111.x` security phase was removed from the active roadmap. It is not an active implementation target unless explicitly reintroduced.

## 28. Deployment Scope

Deployment is intentionally **not** part of the current implementation scope.

Current target:

```text
Local Development
       +
Local Supporting Infrastructure
       +
GitHub
       +
Jenkins CI
```

There is currently no requirement for production Kubernetes, Helm, production Terraform deployment, cloud deployment automation or a production deployment pipeline.

## 29. Simulation vs Real Hardware

TRISHULA models spacecraft, rover, sensors, instruments, communications, mission states and the mission environment in software.

It does not implement real spacecraft hardware or real RF communications.

Outputs are therefore **modelled simulation results**, not measurements from a real spacecraft.

## 30. Current Status

```text
Physical Mission Simulation       VALIDATED
Earth → Moon Mission              VALIDATED
Lunar Orbit                       VALIDATED
Descent / Landing                 VALIDATED
Rover Deployment                  VALIDATED
Rover Autonomous Navigation       VALIDATED
Dynamic Obstacle Handling         VALIDATED
Rover Science                     VALIDATED
Science Data Pipeline             VALIDATED
Science Relay                     VALIDATED
Ground Data Processing            VALIDATED
Command Round Trip                VALIDATED
Closed-Loop Feedback              VALIDATED
Mission Control                   VALIDATED
Ground Data Service               VALIDATED
C++ CTest Suite                   58/58 PASS
Go Tests                          PASS
Go Vet                            PASS
TypeScript                        PASS
Next.js Production Build          PASS
Jenkins CI                        PASS
GitHub Integration                PASS
```

## 31. Remaining Expansion Areas

The core system is substantially integrated. Future incremental expansion areas include:

- deeper spacecraft subsystem models
- broader autonomous rover operations
- broader science observation and science-data capabilities
- additional relay expansion
- long-duration mission operations
- further Mission Control visualization

These should be added incrementally while preserving the validated system.

## 32. Development Rules

1. **Do not restart the project.**
2. **Preserve validated work.**
3. **Implement incrementally.**
4. **Validate important changes.**
5. **Do not invent mission telemetry or state.**
6. **Keep physical simulation and ground processing connected.**
7. **Keep the architecture understandable and interview-explainable.**
8. **Keep Jenkins CI reproducible from GitHub.**
9. **Keep deployment outside the current scope.**

## 33. Final End-to-End Architecture

```text
                         TRISHULA
                            |
          +-----------------+-----------------+
          |                                   |
          v                                   v
   PHYSICAL SIMULATION                  GROUND SYSTEM
        C++                                  Go
          |                                   |
          |                           +-------+-------+
          |                           |       |       |
          |                         Kafka PostgreSQL Redis
          |                           |       |       |
          +-------- Telemetry --------+-------+-------+
                                      |
                                      v
                               REST APIs
                                      |
                                      v
                             MISSION CONTROL
                              Next.js / React
                                      |
                                  Commands
                                      |
                                      v
                              Ground Command
                                      |
                                      v
                           Vehicle Command Adapter
                                      |
                                      v
                           Spacecraft Command Link
                                      |
                                      v
                           Physical Simulation
                                      |
                                      v
                              New Physical State
                                      |
                                      +----> Telemetry
```

Rover and science extend the physical side:

```text
Physical Mission
      ↓
Landing
      ↓
Lander / Vikram
      ↓
Rover
      ├── Mobility
      ├── Localization
      ├── A* Navigation
      ├── Hazard Handling
      └── Science
              ├── LIBS
              ├── APXS
              └── Camera
                    ↓
              Science Pipeline
                    ↓
              Data Products
                    ↓
              Persistent Storage
                    ↓
              Rover → Vikram
                    ↓
                 Ground
                    ↓
            Science Processing
                    ↓
             Mission Knowledge
                    ↓
             Mission Control
```

## 34. Project Links

GitHub:

https://github.com/Debbatisudheer/TRISHULA

Development branch:

https://github.com/Debbatisudheer/TRISHULA/tree/trishula-development

## 35. Final Summary

TRISHULA connects:

```text
Physics
  ↓
Spacecraft
  ↓
Mission Execution
  ↓
Telemetry
  ↓
Ground Data Service
  ↓
Kafka
  ↓
PostgreSQL + Redis
  ↓
REST APIs
  ↓
Mission Control
  ↓
Commands
  ↓
Vehicle Command Path
  ↓
Physical Simulation
  ↓
New Physical State
```

and extends it through:

```text
Landing
  ↓
Rover
  ↓
Autonomous Navigation
  ↓
Hazards
  ↓
Science
  ↓
LIBS / APXS / Camera
  ↓
Science Processing
  ↓
Persistent Data
  ↓
Rover → Vikram → Ground
  ↓
Mission Knowledge
  ↓
Mission Control
```

The project is maintained as an incremental engineering system. GitHub stores the source, `trishula-development` is the active development branch, and Jenkins continuously validates the C++ backend, 58-test CTest suite, Go ground service and Mission Control build chain.

**Current project checkpoint: V0.9.124.1**

**C++ tests: 58/58 passed**

**Jenkins CI: SUCCESS**
