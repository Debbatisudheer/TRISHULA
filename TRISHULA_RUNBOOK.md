# TRISHULA RUNBOOK

## Operational, Development, Validation, CI, Troubleshooting and Recovery Guide

This runbook is the practical operating guide for the TRISHULA project.

It explains how to prepare the environment, obtain the source, build the C++ physical simulation, test the system, run the Go Ground Data Service, run Mission Control, operate local supporting infrastructure, validate the physical mission chain, use Jenkins CI, troubleshoot common failures, recover the project, and preserve validated checkpoints.

> **Important:** TRISHULA is a software-based mission simulation and engineering platform. It is not flight-qualified spacecraft software, real spacecraft hardware, real RF infrastructure, or an official ISRO mission system.

---

# 1. TRISHULA Operating Principle

The most important TRISHULA rule is:

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

Do not replace this with:

```text
UI Button
    ↓
Invented Telemetry
    ↓
Pretend Mission Progress
```

TRISHULA should preserve the connection between modelled physical state, telemetry, ground processing, commands, and Mission Control.

---

# 2. Current Project Checkpoint

Current major checkpoint:

```text
V0.9.124.1
```

Current active development branch:

```text
trishula-development
```

GitHub repository:

```text
https://github.com/Debbatisudheer/TRISHULA.git
```

GitHub development branch:

```text
https://github.com/Debbatisudheer/TRISHULA/tree/trishula-development
```

Current CI job:

```text
TRISHULA-CI
```

Current validated C++ test count:

```text
58
```

Current validation baseline:

```text
C++ CTest       58/58 PASS
Go Tests        PASS
Go Vet          PASS
TypeScript      PASS
Next.js Build   PASS
Jenkins CI      PASS
```

---

# 3. Important Project Rules

## Rule 1 — Do not restart TRISHULA

Do not throw away the existing implementation and start again.

## Rule 2 — Preserve validated work

Existing validated behaviour must remain intact unless a real defect requires a controlled change.

## Rule 3 — Implement incrementally

Add one feature or correction at a time.

## Rule 4 — Validate after important changes

Run the appropriate tests after changes.

## Rule 5 — Do not invent mission state

Mission Control must use authoritative modelled/backend data.

## Rule 6 — Keep the physical and ground loops connected

Telemetry and commands should follow the real software path through the architecture.

## Rule 7 — Keep CI reproducible

Jenkins must build from the Git repository rather than relying on a developer's local working directory.

## Rule 8 — Do not mix deployment into current scope

The current project target is local execution plus Jenkins CI.

## Rule 9 — Architecture study stops at Level 25

The architecture-study phase is complete through Level 25.

Do not continue Level 26+ unless explicitly required later.

## Rule 10 — Security V0.9.111.x is not an active phase

Do not reintroduce the removed security phase unless explicitly requested.

---

# 4. System Architecture

TRISHULA has three major areas:

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

## Physical side

Main technology:

```text
C++
```

Contains modelled:

- physics
- gravity
- perturbations
- dynamics
- numerical integration
- attitude dynamics
- spacecraft state
- propulsion
- sensors
- state estimation
- navigation
- lunar navigation
- terrain-relative navigation
- guidance
- powered descent
- landing dynamics
- rover
- autonomous navigation
- science
- physical mission execution
- runtime control
- ground uplink

## Ground side

Main technology:

```text
Go
```

Contains:

- telemetry ingestion
- event ingestion
- command ingestion
- command validation
- command execution
- acknowledgement
- vehicle command adapter
- spacecraft command link
- command round-trip
- mission data model
- ground processing
- REST APIs
- durable mission history
- current mission state

## Mission Control

Main technology:

```text
Next.js
React
TypeScript
```

---

# 5. Complete Mission Chain

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
  ↓
Lander / Vikram
  ↓
Rover Deployment
  ↓
Rover Mobility
  ↓
Localization
  ↓
A* Autonomous Navigation
  ↓
Hazard Handling
  ↓
Science Operations
  ↓
Science Data Pipeline
  ↓
Rover → Vikram → Ground
  ↓
Ground Processing
  ↓
Mission Knowledge
  ↓
Mission Control
```

---

# 6. Environment Prerequisites

Validated development environment:

```text
Windows
MSYS2 UCRT64
g++
CMake
CTest
Go
Node.js
npm
Docker
Git
Jenkins
```

Validated versions:

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
Git        2.52.0.windows.1
```

C++ toolchain location:

```text
C:\msys64
C:\msys64\ucrt64\bin
```

---

# 7. Windows Toolchain Verification

Open PowerShell.

Check Go:

```powershell
go version
```

Check Node:

```powershell
node --version
```

Check npm:

```powershell
npm --version
```

Check Docker:

```powershell
docker --version
```

Check Git:

```powershell
git --version
```

For the C++ tools, the validated installation is MSYS2 UCRT64.

Open the MSYS2 UCRT64 shell and run:

```bash
g++ --version
gcc --version
cmake --version
ctest --version
which g++
which cmake
```

Expected locations:

```text
/ucrt64/bin/g++
/ucrt64/bin/cmake
```

Windows paths:

```text
C:\msys64\ucrt64\bin\g++.exe
C:\msys64\ucrt64\bin\cmake.exe
C:\msys64\ucrt64\bin\ctest.exe
```

---

# 8. Important Windows Toolchain Rule

Do not install duplicate CMake or g++ installations just because PowerShell cannot find them.

The validated TRISHULA C++ environment is:

```text
MSYS2 UCRT64
```

For Jenkins, direct executable paths are used:

```text
C:\msys64\ucrt64\bin\cmake.exe
C:\msys64\ucrt64\bin\ctest.exe
C:\msys64\ucrt64\bin\g++.exe
```

This avoids problems caused by invoking the UCRT64 launcher from Jenkins PowerShell.

---

# 9. Clone the Repository

Clone the active development branch:

```bash
git clone -b trishula-development https://github.com/Debbatisudheer/TRISHULA.git
cd TRISHULA
```

Verify:

```bash
git branch
```

Expected active branch:

```text
* trishula-development
```

Verify remote:

```bash
git remote -v
```

Expected repository:

```text
https://github.com/Debbatisudheer/TRISHULA.git
```

---

# 10. Git Branch Model

Branches:

```text
master
trishula-development
```

Use:

```text
trishula-development
```

for active development.

Keep:

```text
master
```

as the stable baseline unless project workflow changes later.

---

# 11. Git Status Before Changes

Always check:

```bash
git status
```

Before starting work, ideally:

```text
nothing to commit, working tree clean
```

If you have intentional local changes, understand them before pulling, rebasing, resetting, or deleting anything.

Never blindly run:

```bash
git reset --hard
git clean -fd
```

on a working project.

---

# 12. Repository Structure

High-level structure:

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

Generated directories:

```text
backend/build/
backend/build_jenkins/
mission-control-ui/node_modules/
mission-control-ui/.next/
```

should not be committed.

---

# 13. Backend C++ Build

Go to backend:

```powershell
cd backend
```

Configure:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
```

Build:

```powershell
cmake --build build --parallel 4
```

Run tests:

```powershell
ctest --test-dir build --output-on-failure
```

Expected:

```text
100% tests passed out of 58
```

---

# 14. Clean C++ Build

If a normal local build becomes inconsistent, remove only the generated build directory:

```powershell
Remove-Item -Recurse -Force build
```

Then configure again:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
```

Build:

```powershell
cmake --build build --parallel 4
```

Test:

```powershell
ctest --test-dir build --output-on-failure
```

Do not delete source files.

---

# 15. CMake Troubleshooting

If CMake is not found:

Check:

```powershell
C:\msys64\ucrt64\bin\cmake.exe --version
```

If that works, the installation is fine.

If using MSYS2 UCRT64:

```bash
which cmake
cmake --version
```

Expected:

```text
/ucrt64/bin/cmake
```

If CMake generator errors appear, make sure the build directory is clean before changing generators.

---

# 16. CTest Troubleshooting

Run:

```powershell
ctest --test-dir build --output-on-failure
```

For Jenkins:

```text
C:\msys64\ucrt64\bin\ctest.exe
```

Jenkins does not require CTest JUnit XML for the current pipeline.

The authoritative Jenkins check is the CTest process exit code and test result.

Expected:

```text
100% tests passed out of 58
```

---

# 17. Go Ground Data Service

Location:

```text
backend/services/ground-data-service
```

PowerShell:

```powershell
cd backend\services\ground-data-service
```

Run tests:

```powershell
go test ./...
```

Run vet:

```powershell
go vet ./...
```

Expected:

```text
Go tests: PASS
Go vet: PASS
```

---

# 18. Go Troubleshooting

Check Go:

```powershell
go version
```

Expected validated version:

```text
go1.25.4 windows/amd64
```

If dependencies need refreshing, inspect the existing `go.mod` and `go.sum` before changing them.

Do not arbitrarily upgrade Go modules merely to remove warnings.

Run:

```powershell
go test ./...
```

before and after dependency changes.

---

# 19. PostgreSQL

PostgreSQL is used for durable ground mission history.

Conceptual role:

```text
Mission Records
      ↓
Ground Data Service
      ↓
PostgreSQL
      ↓
Durable Mission History
```

Before starting a ground service that requires PostgreSQL, verify the configured database connection is available.

The exact connection string/environment variables should come from the current service configuration rather than being invented.

---

# 20. Redis

Redis provides current mission state.

Conceptual role:

```text
Ground Data Service
      ↓
Redis
      ↓
Current Mission State
      ↓
Mission Control
```

Before starting services that require Redis, verify the configured Redis address is available.

---

# 21. Kafka

Kafka provides mission-record streaming.

The known ground topic is:

```text
trishula.ground.records
```

Conceptual flow:

```text
Mission Record
      ↓
Kafka
      ↓
Ground Consumer
      ↓
Processing
      ↓
Storage / Current State
```

If Kafka is unavailable, inspect:

- broker availability
- configured broker address
- topic configuration
- consumer group
- service logs

Do not invent new broker/topic values without checking the current configuration.

---

# 22. Docker / Docker Compose

Docker is part of the local infrastructure.

Check:

```powershell
docker --version
```

Check Docker daemon:

```powershell
docker info
```

If Compose is used by the current local configuration:

```powershell
docker compose version
```

List containers:

```powershell
docker ps
```

List all containers:

```powershell
docker ps -a
```

Do not treat local database Docker Compose as production deployment.

---

# 23. Ground Data Service Runtime

The Ground Data Service is a Go service.

Before running it, verify:

```text
PostgreSQL
Redis
Kafka
```

are available when required by the current service configuration.

Known service environment variables have included:

```text
TRISHULA_GROUND_DB_URL
TRISHULA_GROUND_REDIS_ADDR
TRISHULA_GROUND_KAFKA_BROKERS
TRISHULA_GROUND_KAFKA_TOPIC
```

Do not copy old credentials or secret values into Git.

Use local environment configuration.

---

# 24. Ground Service Health

The service has previously reported:

```text
TRISHULA ground data service listening on :8082
```

A health endpoint has also reported a TRISHULA API version in the `v0.9.97` checkpoint.

If the service does not start:

1. Check port 8082.
2. Check PostgreSQL.
3. Check Redis.
4. Check Kafka.
5. Check environment variables.
6. Check Go service logs.
7. Run `go test ./...`.
8. Run `go vet ./...`.

---

# 25. Port 8082 Troubleshooting

Check whether port 8082 is occupied:

```powershell
netstat -ano | findstr :8082
```

Find the process:

```powershell
Get-Process -Id <PID>
```

Do not terminate an unknown process without checking it first.

---

# 26. Mission Control UI

Go to:

```powershell
cd mission-control-ui
```

Install exact lockfile dependencies:

```powershell
npm ci
```

Validate TypeScript:

```powershell
npx tsc --noEmit
```

Build production:

```powershell
npm run build
```

Run development server:

```powershell
npm run dev
```

The UI has previously been run locally on:

```text
http://localhost:3001
```

Use the current terminal output to confirm the actual port if configuration changes.

---

# 27. npm Troubleshooting

If `npm ci` fails:

1. Do not immediately edit `package.json`.
2. Check Node version.
3. Check npm version.
4. Confirm `package-lock.json` matches `package.json`.
5. Read the exact missing package/error.
6. Compare against the Git branch.
7. Make the smallest required correction.

Validated environment:

```text
Node.js 24.11.1
npm 11.6.2
```

The current CI reported:

```text
2 vulnerabilities
1 moderate
1 high
```

This did not fail the build.

Do not automatically run:

```text
npm audit fix --force
```

on the project because it may introduce breaking dependency changes.

---

# 28. Next.js Troubleshooting

Production build:

```powershell
npm run build
```

If it fails:

1. Run `npx tsc --noEmit`.
2. Check the exact Next.js error.
3. Check the affected route/component.
4. Check environment variables.
5. Re-run the build.
6. Avoid unrelated dependency upgrades.

The current production build has a CSS/autoprefixer warning:

```text
autoprefixer: end value has mixed support,
consider using flex-end instead
```

This is currently a warning, not a build failure.

---

# 29. Mission Control Data Rule

Do not add hard-coded mission values just to make the UI look complete.

Correct:

```text
Backend / Modelled State
        ↓
API
        ↓
Mission Control
```

Incorrect:

```text
UI
 ↓
Invented State
```

---

# 30. Physical Mission Operation

The physical mission system contains the modelled mission chain:

```text
Earth
 ↓
TLI
 ↓
Earth-Moon Arc
 ↓
Lunar Approach
 ↓
LOI
 ↓
Lunar Orbit
 ↓
Descent Preparation
 ↓
Powered Descent
 ↓
Terminal Descent
 ↓
Landing
 ↓
Post-Landing
```

Do not manually modify mission state to skip validation unless the specific test/run explicitly requires it.

---

# 31. Rover Operation

Rover flow:

```text
Landing
 ↓
Rover Deployment
 ↓
Mobility
 ↓
Localization
 ↓
A* Navigation
 ↓
Hazard Handling
 ↓
Science
```

Validate rover behaviour using the existing tests before changing navigation logic.

---

# 32. A* Navigation

The rover uses terrain-aware A* autonomous navigation.

When debugging navigation:

1. Confirm rover state.
2. Confirm terrain/environment input.
3. Confirm start state.
4. Confirm goal.
5. Confirm hazard representation.
6. Inspect generated path.
7. Inspect movement.
8. Confirm resulting rover state.
9. Confirm telemetry.

Do not make the UI display a successful route unless the navigation subsystem actually produced it.

---

# 33. Dynamic Obstacles

The rover contains dynamic obstacle validation.

When debugging:

```text
Environment
   ↓
Obstacle State
   ↓
Planner
   ↓
New Path
   ↓
Rover Movement
   ↓
New State
```

Validate with the existing dynamic obstacle tests.

---

# 34. Multi-Objective Rover Planning

TRISHULA contains:

- multi-objective planning
- dynamic multi-objective planning

When modifying this area, preserve:

- navigation validity
- obstacle handling
- mission objectives
- physical/modelled rover state

---

# 35. Science Operation

Science flow:

```text
Rover
 ↓
Instrument Observation
 ↓
Calibration
 ↓
Data Product
 ↓
Integrity / Checksum
 ↓
Persistent Storage
 ↓
Relay
 ↓
Ground
 ↓
Ground Science Processing
 ↓
Mission Knowledge
```

Supported simulated instruments:

```text
LIBS
APXS
Camera
```

---

# 36. Science Troubleshooting

When science data fails:

1. Confirm observation was generated.
2. Confirm instrument output exists.
3. Confirm calibration.
4. Confirm data-product generation.
5. Confirm persistence.
6. Confirm checksum/integrity.
7. Confirm relay.
8. Confirm ground processing.
9. Confirm mission knowledge generation.
10. Confirm Mission Control receives the resulting data.

Do not jump directly to the UI.

---

# 37. Relay Troubleshooting

Relay path:

```text
Rover
 ↓
Vikram
 ↓
Ground
```

Check each hop separately.

A failure at a later hop does not prove the earlier hop is broken.

---

# 38. Telemetry Troubleshooting

Telemetry path:

```text
Physical State
 ↓
Physical Mission
 ↓
Telemetry Loop
 ↓
Ground Uplink
 ↓
Ground Ingestion
 ↓
Kafka / Processing
 ↓
PostgreSQL / Redis
 ↓
REST API
 ↓
Mission Control
```

Troubleshoot in that order.

Do not assume the UI is the source of the problem.

---

# 39. Command Troubleshooting

Command path:

```text
Mission Control
 ↓
REST API
 ↓
Ground Command Ingestion
 ↓
Contract
 ↓
Execution
 ↓
Vehicle Command Adapter
 ↓
Spacecraft Command Link
 ↓
Physical Simulation
 ↓
Telemetry
```

If a command fails, identify the first failed stage.

A previous known runtime issue was:

```text
routing failed: unsupported command execution state 'RECEIVED'
```

When similar state-machine errors occur, inspect the command-state routing/transition logic rather than bypassing the state.

---

# 40. Command Round-Trip Validation

The expected chain is:

```text
Command
 ↓
Ground
 ↓
Vehicle Adapter
 ↓
Spacecraft Command Link
 ↓
Physical Execution
 ↓
State Change
 ↓
Telemetry
 ↓
Ground
 ↓
Mission Control
```

The project contains dedicated command round-trip validation.

Run the full CTest suite after changing command flow.

---

# 41. Ground Storage Troubleshooting

If PostgreSQL persistence fails:

Check:

```text
Database process
Database URL
Database availability
Schema
Service logs
```

If Redis current-state behaviour fails:

Check:

```text
Redis process
Redis address
Service logs
Current-state write/read path
```

If Kafka streaming fails:

Check:

```text
Broker
Topic
Consumer
Consumer group
Service configuration
```

---

# 42. Complete Local Validation

After a significant backend change:

```powershell
cd backend

cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel 4

ctest --test-dir build --output-on-failure
```

Then:

```powershell
cd services\ground-data-service

go test ./...
go vet ./...
```

Then:

```powershell
cd ..\..\..\mission-control-ui

npm ci
npx tsc --noEmit
npm run build
```

Return to repository root:

```powershell
cd ..\..
```

Then:

```powershell
git status
```

---

# 43. Full Jenkins Validation

Jenkins performs the complete CI sequence:

```text
Git checkout
 ↓
Toolchain preflight
 ↓
CMake configure
 ↓
C++ build
 ↓
58 C++ tests
 ↓
Go tests
 ↓
Go vet
 ↓
npm ci
 ↓
TypeScript
 ↓
Next.js production build
 ↓
Artifact packaging
```

The Jenkins workspace is:

```text
C:\ProgramData\Jenkins\.jenkins\workspace\TRISHULA-CI
```

The current pipeline fetches the source from GitHub.

---

# 44. Jenkins Configuration

Validated Jenkins:

```text
Jenkins version:
2.528.3
```

Windows service:

```text
Jenkins
```

Jenkins home:

```text
C:\ProgramData\Jenkins\.jenkins
```

Jenkins executable:

```text
C:\Program Files\Jenkins\jenkins.exe
```

Node:

```text
Built-In Node
```

Node label:

```text
trishula-windows-ucrt64
```

Node environment variables:

```text
MSYS2_ROOT = C:\msys64
UCRT64_BIN = C:\msys64\ucrt64\bin
```

---

# 45. Jenkins Job Configuration

Job:

```text
TRISHULA-CI
```

Pipeline source:

```text
Pipeline script from SCM
```

SCM:

```text
Git
```

Repository:

```text
https://github.com/Debbatisudheer/TRISHULA.git
```

Branch:

```text
*/trishula-development
```

Jenkinsfile:

```text
Jenkinsfile
```

The pipeline uses the Git workspace.

Do not configure it to copy from:

```text
C:\Users\sudhe\sudheer\trishula\TRISHULA
```

The current CI source of truth is GitHub.

---

# 46. Jenkins Node

The node is a Windows machine with two executors.

The relevant label is:

```text
trishula-windows-ucrt64
```

Check:

```text
Manage Jenkins
    ↓
Nodes
    ↓
Built-In Node
```

Verify:

- online
- label present
- UCRT64 paths
- sufficient disk
- required tools available

---

# 47. Jenkins Pipeline Stages

The current Git-based Jenkins pipeline contains these major stages:

```text
Checkout
Toolchain Preflight
Backend - CMake Configure
Backend - C++ Build
Backend - C++ Tests
Ground Data Service - Go Tests
Ground Data Service - Go Vet
Mission Control UI - Install
Mission Control UI - TypeScript
Mission Control UI - Production Build
Package CI Artifacts
```

---

# 48. Jenkins C++ Toolchain

Jenkins should use direct UCRT64 executables:

```text
C:\msys64\ucrt64\bin\g++.exe
C:\msys64\ucrt64\bin\cmake.exe
C:\msys64\ucrt64\bin\ctest.exe
```

The pipeline should set:

```text
PATH = UCRT64_BIN + existing PATH
```

for C++ stages.

Avoid relying on:

```text
C:\msys64\ucrt64.exe -lc
```

inside Jenkins PowerShell because earlier validation showed unreliable command/output and exit-code handling through that launcher.

---

# 49. Jenkins Robocopy Rule

If a future pipeline uses Robocopy:

Robocopy exit codes `0` through `7` can represent successful or successful-with-differences outcomes.

Only values greater than `7` should be treated as failure for the snapshot operation.

Do not blindly treat exit code `1` as a failure.

---

# 50. Jenkins Test Rule

Do not require CTest JUnit XML unless the pipeline explicitly generates it.

The current validated pipeline uses:

```powershell
ctest --test-dir build_jenkins --output-on-failure
```

and checks:

```text
$LASTEXITCODE
```

Expected:

```text
100% tests passed out of 58
```

---

# 51. Jenkins Artifact Rule

The current pipeline packages CI artifacts such as:

```text
CMakeCache.txt
UI-BUILD_ID
package.json
package-lock.json
```

Artifacts are archived by Jenkins.

These artifacts are CI evidence, not production deployment artifacts.

---

# 52. Jenkins Workspace Cleanup

The pipeline uses:

```text
deleteDir()
```

in the cleanup stage.

Therefore the Jenkins workspace is temporary.

The source of truth remains:

```text
GitHub
```

not the Jenkins workspace.

---

# 53. Jenkins Failure Procedure

If Jenkins fails:

1. Open the failed build.
2. Open Console Output.
3. Find the first meaningful failure.
4. Ignore later cascading failures until the first failure is understood.
5. Identify the stage.
6. Reproduce locally if practical.
7. Fix the smallest required issue.
8. Commit the fix.
9. Push to `trishula-development`.
10. Run Jenkins again.

Do not repeatedly rerun a failing pipeline without understanding the failure.

---

# 54. Jenkins Build Cancellation

If a Jenkins build becomes stuck:

First use the normal Jenkins stop action.

If it does not stop, inspect the build executor.

A Jenkins Script Console can inspect a build:

```groovy
def job = Jenkins.instance.getItemByFullName("TRISHULA-CI")
def build = job?.getBuildByNumber(15)

println("Build #15 building: " + build.isBuilding())
println("Build #15 result: " + build.getResult())
```

For a currently running build, Jenkins' executor can be inspected:

```groovy
def job = Jenkins.instance.getItemByFullName("TRISHULA-CI")
def build = job?.getBuildByNumber(15)

def executor = build?.getExecutor()

println("Executor: " + executor)
```

Use the exact build number that is actually stuck.

Do not instantiate `hudson.triggers.SafeTimerTask`; it is abstract and cannot be instantiated.

---

# 55. Deleting an Old Jenkins Job

If a complete Jenkins job reset is intentionally required, the Script Console can delete the job:

```groovy
def jobName = "TRISHULA-CI"
def job = Jenkins.instance.getItemByFullName(jobName)

if (job == null) {
    println("JOB NOT FOUND: " + jobName)
} else {
    job.delete()
    println("JOB DELETED: " + jobName)
}
```

This deletes the job and its build history.

Use only when intentionally resetting the Jenkins job.

---

# 56. Recreating TRISHULA-CI

Create:

```text
New Item
```

Name:

```text
TRISHULA-CI
```

Select:

```text
Pipeline
```

Configure:

```text
Definition:
Pipeline script from SCM

SCM:
Git

Repository:
https://github.com/Debbatisudheer/TRISHULA.git

Branch:
*/trishula-development

Script Path:
Jenkinsfile
```

Save and run.

---

# 57. GitHub → Jenkins Workflow

Normal development flow:

```text
Developer
   ↓
Local TRISHULA change
   ↓
Local validation
   ↓
git status
   ↓
git add
   ↓
git commit
   ↓
git push origin trishula-development
   ↓
GitHub
   ↓
Jenkins TRISHULA-CI
   ↓
Checkout
   ↓
Build
   ↓
Tests
   ↓
UI validation
   ↓
SUCCESS / FAILURE
```

---

# 58. Git Commit Workflow

Before commit:

```bash
git status
```

Review:

```bash
git diff
```

Stage:

```bash
git add <specific-files>
```

Review staged changes:

```bash
git diff --cached
```

Commit:

```bash
git commit -m "Describe the change"
```

Push:

```bash
git push origin trishula-development
```

Then monitor Jenkins.

---

# 59. Git Ignore Rules

Generated files should remain ignored:

```text
backend/build/
backend/build_jenkins/
mission-control-ui/node_modules/
mission-control-ui/.next/
*.tsbuildinfo
```

Do not force-add generated build output unless there is a specific project requirement.

---

# 60. Git Large File Problems

If Git reports large files:

1. Identify the file.
2. Determine whether it is generated.
3. If generated, add it to `.gitignore`.
4. If already staged, unstage it.
5. If already committed, history may need controlled cleanup.
6. Do not delete legitimate project assets just to make a push succeed.

Previously, `.terraform` provider binaries were an example of generated content that should not be committed.

---

# 61. Mission Control Runtime Validation

When Mission Control is running:

1. Confirm UI starts.
2. Confirm backend service is reachable.
3. Confirm required infrastructure is available.
4. Confirm health/API endpoint.
5. Confirm mission data appears.
6. Confirm telemetry is modelled/backend sourced.
7. Confirm commands use the actual command path.
8. Confirm rover data comes from the rover subsystem.
9. Confirm science data comes from the science path.

---

# 62. End-to-End Physical Telemetry Validation

Validate:

```text
Physical Simulation
      ↓
Mission State
      ↓
Telemetry
      ↓
Ground Uplink
      ↓
Ground Ingestion
      ↓
Processing
      ↓
Storage
      ↓
REST API
      ↓
Mission Control
```

The telemetry should correspond to the modelled physical state.

---

# 63. End-to-End Command Validation

Validate:

```text
Mission Control
      ↓
Ground API
      ↓
Command Ingestion
      ↓
Contract
      ↓
Execution
      ↓
Vehicle Adapter
      ↓
Command Link
      ↓
Physical State
      ↓
Telemetry
      ↓
Ground
      ↓
Mission Control
```

A successful HTTP response alone is not sufficient to claim a full command round-trip.

---

# 64. Rover End-to-End Validation

Validate:

```text
Rover Deployment
 ↓
Mobility
 ↓
Localization
 ↓
A*
 ↓
Obstacle Handling
 ↓
Movement
 ↓
Science
 ↓
Relay
 ↓
Ground
 ↓
Mission Control
```

---

# 65. Science End-to-End Validation

Validate:

```text
Observation
 ↓
Instrument
 ↓
Calibration
 ↓
Data Product
 ↓
Integrity
 ↓
Persistent Storage
 ↓
Relay
 ↓
Ground
 ↓
Science Processing
 ↓
Mission Knowledge
 ↓
Mission Control
```

---

# 66. Complete System Validation Checklist

Before declaring a major checkpoint valid:

```text
[ ] Git status understood
[ ] Correct branch
[ ] CMake configure passes
[ ] C++ build passes
[ ] 58 C++ tests pass
[ ] Go tests pass
[ ] Go vet passes
[ ] npm ci passes
[ ] TypeScript passes
[ ] Next.js build passes
[ ] Ground service starts if required
[ ] PostgreSQL available if required
[ ] Redis available if required
[ ] Kafka available if required
[ ] Telemetry path validated
[ ] Command path validated
[ ] Command round-trip validated
[ ] Rover validation passes
[ ] Science validation passes
[ ] Relay validation passes
[ ] Mission Control receives authoritative data
[ ] Jenkins CI passes
[ ] Git commit created
[ ] GitHub branch updated
```

---

# 67. Clean Validation From Git

For the strongest CI-style validation:

```text
GitHub
  ↓
Fresh Jenkins workspace
  ↓
Checkout
  ↓
CMake configure
  ↓
C++ build
  ↓
58 tests
  ↓
Go tests
  ↓
Go vet
  ↓
npm ci
  ↓
TypeScript
  ↓
Next.js build
```

This reduces dependence on local generated files.

---

# 68. What Not To Do

Do not:

- restart the entire project
- replace working architecture without evidence
- invent telemetry
- hard-code mission state into UI
- bypass command state validation
- randomly upgrade dependencies
- delete source directories to solve build problems
- commit `node_modules`
- commit `.next`
- commit C++ build directories
- commit secrets
- use production deployment assumptions
- claim real spacecraft/RF capability
- continue architecture-study levels beyond 25 without explicit decision
- reintroduce removed Security V0.9.111.x scope without explicit decision

---

# 69. Known Warnings That Do Not Automatically Mean Failure

### npm audit

Current CI reported:

```text
2 vulnerabilities
1 moderate
1 high
```

This did not fail `npm ci`.

Investigate separately before changing dependencies.

### Next.js no-cache

A message such as:

```text
No build cache found
```

is not itself a build failure.

### Autoprefixer

Current warning:

```text
autoprefixer: end value has mixed support,
consider using flex-end instead
```

The production build can still succeed.

### Funding messages

npm package funding messages are informational.

---

# 70. First Failure Principle

When a pipeline fails:

```text
Find the first real error.
```

Do not start fixing the last message automatically.

Example:

```text
CMake failed
    ↓
Later tests fail
```

The later test failures may be consequences of the CMake failure.

Fix the first root cause.

---

# 71. Environment Variable Rule

Never commit secrets.

For local development, environment variables may be configured in the local shell.

Previously used ground-service variables include:

```text
TRISHULA_GROUND_DB_URL
TRISHULA_GROUND_REDIS_ADDR
TRISHULA_GROUND_KAFKA_BROKERS
TRISHULA_GROUND_KAFKA_TOPIC
```

Use the current project configuration to determine actual values.

Do not place passwords, tokens or private credentials in:

- Git
- Jenkinsfile
- README
- screenshots
- public documentation

---

# 72. Port Troubleshooting

Useful Windows command:

```powershell
netstat -ano
```

For a specific port:

```powershell
netstat -ano | findstr :8082
```

Then:

```powershell
Get-Process -Id <PID>
```

For the UI, check the terminal where `npm run dev` is running for the actual assigned port.

---

# 73. Jenkins Service Troubleshooting

Check Windows service:

```powershell
Get-Service Jenkins
```

Expected:

```text
Status: Running
```

If necessary, inspect:

```powershell
Get-Service Jenkins | Format-List *
```

Jenkins home:

```text
C:\ProgramData\Jenkins\.jenkins
```

Do not delete Jenkins home casually.

---

# 74. Jenkins Authentication / Security

The Jenkins instance has security enabled.

Do not disable Jenkins security as a normal troubleshooting step.

If authentication problems occur, use the supported Jenkins administration/recovery procedure and preserve the existing configuration.

Do not expose Jenkins password hashes or credentials.

---

# 75. Jenkins Script Console Safety

Jenkins Script Console has administrator-level power.

Use scripts only when the exact effect is understood.

Before running destructive scripts:

- confirm job name
- confirm build number
- confirm intended target
- avoid broad wildcard deletion
- do not delete Jenkins home

---

# 76. Backup Principle

Before major Jenkins administration changes, preserve important configuration.

Examples:

```text
Jenkins config
Jenkins job configuration
Jenkinsfile in Git
Git repository
```

The Git repository is the primary project source of truth.

---

# 77. Version Checkpoint Procedure

When a major checkpoint is validated:

1. Run local validation.
2. Run Jenkins CI.
3. Confirm all required tests.
4. Confirm Git status.
5. Commit.
6. Push.
7. Record the version.
8. Record what changed.
9. Preserve the previous validated checkpoint.
10. Continue incrementally.

Example:

```text
V0.9.124.1
Jenkins CI baseline
58/58 C++ tests
Go PASS
UI PASS
Jenkins PASS
```

---

# 78. Recovery From a Broken Local Build

If only generated build output is broken:

```powershell
Remove-Item -Recurse -Force backend\build
```

Then rebuild.

If UI generated output is broken:

```powershell
Remove-Item -Recurse -Force mission-control-ui\.next
```

Then:

```powershell
cd mission-control-ui
npm ci
npm run build
```

Do not delete source code.

---

# 79. Recovery From a Broken Jenkins Workspace

The Jenkins workspace is disposable.

A fresh build can recreate:

```text
C:\ProgramData\Jenkins\.jenkins\workspace\TRISHULA-CI
```

The important source is:

```text
GitHub
```

not the workspace.

The pipeline also uses:

```text
deleteDir()
```

during cleanup.

---

# 80. Recovery From a Bad Git Commit

First inspect:

```bash
git log --oneline --decorate -10
```

Inspect changes:

```bash
git show <commit>
```

Do not immediately rewrite shared branch history.

If a bad commit has already been pushed, prefer a controlled corrective commit unless there is an explicit decision to rewrite history.

---

# 81. Local vs CI Difference

Local builds use:

```text
backend/build
```

Jenkins uses:

```text
backend/build_jenkins
```

Local UI uses:

```text
mission-control-ui/node_modules
mission-control-ui/.next
```

Jenkins creates its own workspace and installs dependencies from the lockfile.

Therefore:

```text
Local success
```

and:

```text
Fresh Jenkins success
```

are both useful, but Jenkins provides stronger clean-workspace validation.

---

# 82. Current Jenkins Success Baseline

The latest validated Jenkins run demonstrated:

```text
Git checkout                 PASS
Toolchain preflight          PASS
CMake configure              PASS
C++ build                    PASS
C++ tests                    58/58 PASS
Go tests                     PASS
Go vet                       PASS
npm ci                       PASS
TypeScript                   PASS
Next.js build                PASS
Artifacts                    PACKAGED
Overall                      SUCCESS
```

The current repository source was fetched from:

```text
https://github.com/Debbatisudheer/TRISHULA.git
```

and the active branch was:

```text
trishula-development
```

---

# 83. CI Maintenance Rules

When editing the Jenkinsfile:

1. Keep Git checkout.
2. Keep the `trishula-windows-ucrt64` agent.
3. Keep direct UCRT64 executable paths.
4. Keep CMake configure.
5. Keep C++ build.
6. Keep CTest.
7. Keep Go tests.
8. Keep Go vet.
9. Keep npm CI.
10. Keep TypeScript.
11. Keep Next.js build.
12. Keep artifact packaging.
13. Avoid deployment stages unless the project scope explicitly changes.

---

# 84. CI Does Not Deploy

The current Jenkins pipeline is:

```text
CI
```

not:

```text
CI/CD deployment
```

Jenkins validates the project.

It does not currently deploy TRISHULA to production infrastructure.

---

# 85. Docker Does Not Mean Production

Docker is part of local supporting infrastructure.

Do not describe the project as production-deployed simply because Docker or Docker Compose exists.

---

# 86. Physical Simulation Boundary

The physical subsystem is modelled.

It can calculate:

- mission states
- trajectory behaviour
- rover movement
- science observations
- telemetry
- command effects

within the simulation model.

It does not represent physical measurements from real hardware.

---

# 87. RF / Communications Boundary

The software may model command and relay paths.

It does not implement:

```text
Real spacecraft RF
Real antenna hardware
Real radio hardware
Real lunar communication link
```

Do not describe the software as having real RF capability.

---

# 88. Mission Control Boundary

Mission Control is an interface to the modelled/ground system.

It should not become a second mission-state engine.

Preferred:

```text
Backend / Physical Model
        ↓
Ground
        ↓
API
        ↓
UI
```

---

# 89. Future Work Rule

Future work should be selected from actual incomplete areas.

Known expansion areas include:

- deeper spacecraft subsystem models
- broader autonomous rover operations
- broader science observations/data
- additional relay expansion
- long-duration operations
- further Mission Control visualization

Do not create duplicate systems for capabilities that already exist.

---

# 90. Architecture Study Boundary

The project architecture study is complete through:

```text
Level 25
```

After Level 25:

```text
Architecture Study
        ↓
STOP
        ↓
Implementation
        ↓
Validation
```

---

# 91. Security Boundary

The removed security phase:

```text
V0.9.111.x
```

is not part of the active roadmap.

Do not accidentally recreate it in future documentation, Jenkins stages, or version planning.

---

# 92. Deployment Boundary

Current deployment decision:

```text
No production deployment implementation.
```

Focus remains:

```text
Local
+
GitHub
+
Jenkins CI
```

---

# 93. Interview Explanation

A simple explanation of TRISHULA:

> TRISHULA is a software-based closed-loop lunar mission simulation and mission-control platform. The physical side is implemented in C++, the ground data system is implemented in Go, and Mission Control is built with Next.js and React. The physical simulation generates modelled mission state and telemetry, the ground system ingests and processes telemetry, events and commands using Kafka, PostgreSQL and Redis, and Mission Control exposes the mission state through REST APIs. The rover supports A* autonomous navigation, hazard handling and simulated LIBS, APXS and camera science. Science data can move through a Rover → Vikram → Ground relay path and into ground processing and mission knowledge. Jenkins CI fetches the development branch from GitHub and validates the C++ build and 58-test CTest suite, Go tests/vet, TypeScript and Next.js production build.

---

# 94. Operational Quick Reference

## Clone

```bash
git clone -b trishula-development https://github.com/Debbatisudheer/TRISHULA.git
```

## C++

```powershell
cd backend
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

## Go

```powershell
cd backend\services\ground-data-service
go test ./...
go vet ./...
```

## UI

```powershell
cd mission-control-ui
npm ci
npx tsc --noEmit
npm run build
npm run dev
```

## Git

```bash
git status
git add <files>
git commit -m "Describe change"
git push origin trishula-development
```

## Jenkins

```text
TRISHULA-CI
    ↓
GitHub
    ↓
trishula-development
    ↓
Build
    ↓
58 C++ Tests
    ↓
Go
    ↓
UI
    ↓
SUCCESS
```

---

# 95. Final Pre-Release Checklist

Before recording a major TRISHULA checkpoint:

```text
[ ] Correct Git branch
[ ] Working tree understood
[ ] No accidental generated files
[ ] CMake configure PASS
[ ] C++ build PASS
[ ] 58/58 C++ tests PASS
[ ] Go tests PASS
[ ] Go vet PASS
[ ] npm ci PASS
[ ] TypeScript PASS
[ ] Next.js build PASS
[ ] Required PostgreSQL available
[ ] Required Redis available
[ ] Required Kafka available
[ ] Ground Data Service validated
[ ] Telemetry path validated
[ ] Command path validated
[ ] Command round-trip validated
[ ] Rover validated
[ ] Science validated
[ ] Relay validated
[ ] Mission Control validated
[ ] Jenkins CI PASS
[ ] Git commit created
[ ] GitHub branch pushed
[ ] Version/checkpoint recorded
```

---

# 96. Final Recovery Checklist

If something breaks:

```text
1. Stop.
2. Do not restart the architecture.
3. Check Git status.
4. Identify the first failing layer.
5. Reproduce locally.
6. Read the actual error.
7. Fix the smallest required component.
8. Run the relevant test.
9. Run the full validation.
10. Run Jenkins.
11. Commit only the intended change.
12. Push to trishula-development.
13. Record the checkpoint.
```

---

# 97. Final TRISHULA Operational Model

The complete system is:

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

Rover/science extension:

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

---

# 98. Current Final Checkpoint

```text
Project:
TRISHULA

Version:
V0.9.124.1

Repository:
https://github.com/Debbatisudheer/TRISHULA.git

Active Branch:
trishula-development

CI:
Jenkins

CI Job:
TRISHULA-CI

C++ Tests:
58/58 PASS

Go Tests:
PASS

Go Vet:
PASS

TypeScript:
PASS

Next.js Production Build:
PASS

Jenkins:
SUCCESS

Deployment:
Not implemented / intentionally out of current scope

Architecture Study:
Completed through Level 25

Security V0.9.111.x:
Removed from active roadmap
```

---

# 99. End of Runbook

This runbook is the operational reference for the current TRISHULA checkpoint.

The most important rule remains:

```text
Preserve the validated system.
Implement incrementally.
Validate every important change.
Keep the physical → ground → Mission Control → command → physical loop intact.
```

**TRISHULA V0.9.124.1**
