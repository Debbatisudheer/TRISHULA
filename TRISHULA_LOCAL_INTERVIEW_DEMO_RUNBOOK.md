# TRISHULA Local Interview Demo Runbook

**Purpose:** Restore and run TRISHULA locally for an interview/demo after a long gap.

## 1. Project

Expected location:

```powershell
C:\Users\sudhe\sudheer\trishula\TRISHULA
```

Structure:

```text
TRISHULA/
├── backend/
├── mission-control-ui/
├── Jenkinsfile
└── ...
```

TRISHULA is a software-based closed-loop physical lunar mission simulation and Mission Control platform.

Core loop:

```text
Physical State
      ↓
Physical Computation
      ↓
Physical Action
      ↓
New Physical State
      ↓
Telemetry / Feedback
      ↓
Ground
      ↓
Mission Control
```

## 2. Local Architecture

```text
C++ Physical Simulation
        ↓
Telemetry / Ground Uplink
        ↓
Go Ground Data Service :8082
        ↓
 ┌──────┼────────┐
 ▼      ▼        ▼
Kafka PostgreSQL Redis
 └──────┼────────┘
        ▼
Next.js / React Mission Control
```

Main technologies:

- C++ — physical simulation and vehicle-side systems
- Go — ground data and command services
- Next.js / React / TypeScript — Mission Control
- PostgreSQL — durable mission/ground data
- Redis — current/fast state
- Kafka — mission record/event streaming
- Docker — local infrastructure
- CMake / CTest — C++ build and validation
- Git / GitHub — source control
- Jenkins — CI validation

This runbook is for **local execution and interview demonstration**. Vercel is not required.

## 3. Step 1 — Check Git

```powershell
cd C:\Users\sudhe\sudheer\trishula\TRISHULA

git status
git branch
git log -1 --oneline
git pull
```

Do not modify source code just to restore the environment.

## 4. Step 2 — Verify Toolchain

```powershell
java -version
go version
node --version
npm --version
docker --version
docker compose version
```

Previously validated environment:

```text
Java    17.0.17
Go      1.25.4
Node    24.11.1
npm     11.6.2
Docker  29.5.3
```

C++ uses MSYS2 UCRT64:

```powershell
C:\msys64\ucrt64\bin\cmake.exe --version
C:\msys64\ucrt64\bin\g++.exe --version
C:\msys64\ucrt64\bin\ctest.exe --version
```

For the current PowerShell session:

```powershell
$env:MSYS2_ROOT="C:\msys64"
$env:UCRT64_BIN="C:\msys64\ucrt64\bin"
$env:PATH="$env:UCRT64_BIN;$env:PATH"
```

Then:

```powershell
cmake --version
g++ --version
ctest --version
```

## 5. Step 3 — Start Local Infrastructure

TRISHULA uses:

```text
PostgreSQL → durable mission/ground history
Redis      → current/fast mission state
Kafka      → mission record/event streaming
```

Kafka topic:

```text
trishula.ground.records
```

Check Docker:

```powershell
docker ps
```

Find the current Compose configuration:

```powershell
Get-ChildItem -Recurse -Include docker-compose.yml,docker-compose.yaml,compose.yml,compose.yaml
```

Use the Compose configuration already present in the project. Do not invent a new Compose file or new ports.

## 6. Step 4 — Build C++

```powershell
cd C:\Users\sudhe\sudheer\trishula\TRISHULA\backend

cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Previously validated checkpoint:

```text
58/58 tests passed
100% tests passed
```

A previously validated physical mission ground-uplink executable is:

```text
trishula_physical_mission_ground_uplink.exe
```

## 7. Step 5 — Verify Go Ground Data Service

Open a new PowerShell and go to the current Ground Data Service directory under:

```text
backend/services/ground-data-service
```

Run:

```powershell
go test ./...
go vet ./...
```

Important environment variables:

```text
TRISHULA_GROUND_DB_URL
TRISHULA_GROUND_REDIS_ADDR
TRISHULA_GROUND_KAFKA_BROKERS
TRISHULA_GROUND_KAFKA_TOPIC
```

Previously used local PostgreSQL connection:

```text
postgres://trishula:trishula_dev@localhost:5433/trishula_ground?sslmode=disable
```

Use the current local project configuration. Do not copy production credentials.

## 8. Step 6 — Start Ground Data Service

After PostgreSQL, Redis and Kafka are available, start the Ground Data Service using the current project instructions.

Expected message:

```text
TRISHULA ground data service listening on :8082
```

Check the port:

```powershell
netstat -ano | findstr :8082
```

The Ground API uses local port:

```text
8082
```

## 9. Step 7 — Start Physical Mission

From the backend directory:

```powershell
cd C:\Users\sudhe\sudheer\trishula\TRISHULA\backend
```

Run the validated physical mission ground-uplink executable:

```powershell
.\build\trishula_physical_mission_ground_uplink.exe
```

Flow:

```text
C++ Physical Mission
        ↓
Physical telemetry
        ↓
Ground Uplink
        ↓
Go Ground API :8082
        ↓
PostgreSQL / Redis / Kafka
        ↓
Mission Control
```

The simulation is software-modelled. Do not describe it as real spacecraft hardware or real RF communication.

## 10. Step 8 — Start Mission Control

Open another PowerShell:

```powershell
cd C:\Users\sudhe\sudheer\trishula\TRISHULA\mission-control-ui
```

Install dependencies:

```powershell
npm ci
```

Check TypeScript:

```powershell
npx tsc --noEmit
```

Production build:

```powershell
npm run build
```

For the interview:

```powershell
npm run dev
```

Use the port printed by Next.js. A previously used local port was:

```text
http://localhost:3001
```

## 11. Recommended Startup Order

```text
1. Docker infrastructure
        ↓
2. PostgreSQL
3. Redis
4. Kafka
        ↓
5. Go Ground Data Service :8082
        ↓
6. C++ Physical Mission
        ↓
7. Next.js Mission Control
        ↓
8. Browser
```

## 12. Interview Demo Flow

### Mission Control

Explain:

> This is the Mission Control layer. It visualizes authoritative mission state coming from the backend.

### Physical Simulation

Explain:

> The physical side is modelled in C++. Mission progression comes from physical computation and state transitions rather than a UI timer.

### Telemetry

```text
C++ simulation
      ↓
Telemetry
      ↓
Ground API
      ↓
Storage / streaming
      ↓
Mission Control
```

### Ground System

```text
Go
 ↓
REST API
 ↓
Kafka
 ↓
PostgreSQL
 ↓
Redis
```

Simple explanation:

- PostgreSQL stores durable mission history.
- Redis provides fast/current state.
- Kafka streams mission records/events.

### Command Round Trip

```text
Mission Control
      ↓
Command API
      ↓
Ground command processing
      ↓
Vehicle / spacecraft command path
      ↓
Physical state change
      ↓
Telemetry
      ↓
Ground
      ↓
Mission Control
```

Previously validated command endpoint:

```text
POST /v1/missions/{missionID}/commands
```

Explain:

> A command is not considered complete just because the UI sent it. It goes through the ground and vehicle command path, produces an execution result, and the resulting state is reflected back through telemetry.

### Rover

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
Re-plan
```

### Science

```text
LIBS
APXS
Camera
   ↓
Science Processing
   ↓
Rover
   ↓
Lander / Vikram
   ↓
Ground
   ↓
Mission Control
```

These are software-modelled systems and processing pipelines.

## 13. AI vs Autonomy

If asked:

**AI** can provide perception, classification, prediction, anomaly detection, learned models and intelligent data interpretation.

**Autonomy** means the system can:

```text
Sense
 ↓
Estimate State
 ↓
Evaluate Situation
 ↓
Decide
 ↓
Act
 ↓
Observe Result
 ↓
Repeat
```

AI is not required for every autonomous function. Classical algorithms, control systems, navigation, rules, constraints and state estimation can provide autonomy.

## 14. Important Project Boundary

Use this wording:

> TRISHULA is a software-based physical mission simulation and Mission Control platform. It models spacecraft, lunar, rover, science, ground, and command/telemetry behaviour. It is not flight-qualified spacecraft software and does not contain real RF or physical spacecraft hardware.

## 15. Quick Health Checklist

```text
[ ] Git repository available
[ ] Correct branch checked
[ ] Java available
[ ] Go available
[ ] Node/npm available
[ ] Docker available
[ ] MSYS2 UCRT64 available
[ ] CMake available
[ ] g++ available
[ ] CTest available
[ ] PostgreSQL running
[ ] Redis running
[ ] Kafka running
[ ] Ground Data Service running on :8082
[ ] C++ build successful
[ ] CTest passing
[ ] Physical mission executable available
[ ] Mission Control npm install successful
[ ] Mission Control production build successful
[ ] Mission Control dev server running
[ ] Browser can open Mission Control
[ ] Telemetry path verified
[ ] Command path verified
[ ] Rover demo verified
[ ] Science demo verified
```

## 16. Troubleshooting Order

Do not immediately modify source code.

```text
1. Check Git branch/status
2. Check toolchain
3. Check Docker
4. Check PostgreSQL
5. Check Redis
6. Check Kafka
7. Check Ground API :8082
8. Check C++ build
9. Check CTest
10. Check Mission Control
11. Only then investigate source code
```

Useful commands:

```powershell
git status
git branch
docker ps
netstat -ano | findstr :8082
go test ./...
ctest --test-dir build --output-on-failure
npm run build
```

## 17. Core TRISHULA Explanation

If the interviewer asks:

**"How does TRISHULA work?"**

Use:

> TRISHULA is a closed-loop physical lunar mission simulation. The physical side is modelled in C++, the ground side is implemented in Go, and Mission Control is built with Next.js. The simulation produces physical state and telemetry, the ground system receives and stores that information using REST, Kafka, PostgreSQL and Redis, and Mission Control visualizes the authoritative state. Commands travel in the opposite direction through the ground and vehicle command path and eventually affect the modelled physical state. The resulting state is then reflected back through telemetry.

## 18. Final Mental Model

```text
Mission Control
      ↓
Command
      ↓
Ground
      ↓
Physical Simulation
      ↓
State Change
      ↓
Telemetry
      ↓
Ground
      ↓
Mission Control
```

This is the closed-loop demonstration to prioritize.

## 19. Local Demo Architecture

```text
                     HUMAN / INTERVIEWER
                              │
                              ▼
                  ┌──────────────────────┐
                  │   MISSION CONTROL    │
                  │ Next.js / React / TS │
                  └──────────┬───────────┘
                             │
                       REST / Commands
                             │
                             ▼
                  ┌──────────────────────┐
                  │   GROUND SERVICES    │
                  │        Go            │
                  │       :8082          │
                  └──────────┬───────────┘
                             │
             ┌───────────────┼───────────────┐
             ▼               ▼               ▼
         PostgreSQL        Redis           Kafka
         History          State           Stream
             │               │               │
             └───────────────┼───────────────┘
                             │
                             ▼
                  ┌──────────────────────┐
                  │     C++ PHYSICS      │
                  │                      │
                  │ Spacecraft           │
                  │ Lunar Mission        │
                  │ Lander               │
                  │ Rover                │
                  │ Science              │
                  └──────────┬───────────┘
                             │
                        Physical State
                             │
                             ▼
                         Telemetry
                             │
                             └──────────────► Ground
```

## 20. Golden Rule for the Interview

Do not try to demonstrate every feature.

Demonstrate one complete closed loop:

```text
Mission Control
      ↓
Command
      ↓
Ground
      ↓
Physical Simulation
      ↓
State Change
      ↓
Telemetry
      ↓
Ground
      ↓
Mission Control
```

Then explain how rover, science, navigation, Kafka, Redis, PostgreSQL and autonomy fit into the architecture.

---

**Status:** Local interview-demo restoration guide.

**Deployment:** Vercel deployment is separate and not required for the local demo.

**Project principle:** Preserve the existing validated TRISHULA implementation. Restore and verify first; modify code only when an actual defect is identified.
