# TRISHULA: WebSocket Usage and Purpose

## Beginner-Friendly Explanation

This document explains **where TRISHULA uses WebSocket, why it is used, and how it fits into the overall architecture**.

The goal is to make the concept easy to understand even for someone who is new to WebSocket, real-time systems, or mission-control software.

---

# 1. What is WebSocket?

WebSocket is a communication technology that allows a client and server to keep a connection open and exchange messages in real time.

With a normal REST API, the communication usually looks like:

```text
Mission Control UI
       |
       | Request
       v
Backend
       |
       | Response
       v
Mission Control UI
```

The UI has to make another request when it wants new information.

With WebSocket:

```text
Mission Control UI
       <================>
            Backend

        Open connection
        Live communication
```

The connection can stay open, allowing the backend to send updates to the UI as they happen.

---

# 2. Why does TRISHULA need WebSocket?

TRISHULA is a mission-control system.

Mission Control needs to display changing information such as:

```text
Telemetry
Mission state
Vehicle state
Rover state
Events
Command status
```

These values can change continuously.

For example:

```text
Battery

82.1%
81.9%
81.7%
81.5%
...
```

The Mission Control UI should be able to receive these changes without constantly refreshing the page.

That is where WebSocket fits.

---

# 3. The main purpose of WebSocket in TRISHULA

The simplest explanation is:

> **WebSocket is used as a real-time communication path between the TRISHULA backend/current-state side and the Mission Control UI.**

It allows live mission information to be pushed toward the UI.

Conceptually:

```text
Backend
   |
   | Live update
   v
WebSocket
   |
   v
Mission Control UI
```

---

# 4. Where WebSocket fits in the TRISHULA architecture

The documented real-time flow is approximately:

```text
C++ Physical Simulation
          |
          | Telemetry / events
          v
        Kafka
          |
          v
   State Processor
          |
          v
        Redis
          |
          v
      WebSocket
          |
          v
 Mission Control UI
```

Each component has a different responsibility.

---

# 5. What Kafka does

Kafka is used for internal event/data transport.

For example:

```text
Telemetry
    |
    v
Kafka
    |
    +------> Ground services
    |
    +------> Processing
    |
    +------> Other consumers
```

Think of Kafka as an internal event/data pipeline.

It is not the same thing as WebSocket.

---

# 6. What Redis does

Redis is used for fast/current state.

For example:

```text
Latest spacecraft state
Latest rover state
Latest mission state
```

Conceptually:

```text
Kafka
  |
  v
State Processor
  |
  v
Redis
  |
  v
Current state
```

Redis allows the system to quickly access current mission information.

---

# 7. What WebSocket does

WebSocket takes the real-time/current information and makes it available to the Mission Control UI through a persistent connection.

Conceptually:

```text
Kafka
  |
  v
State Processor
  |
  v
Redis
  |
  v
WebSocket
  |
  v
Mission Control UI
```

So a useful mental model is:

```text
Kafka     = internal event/data transport

Redis     = fast current state

WebSocket = real-time connection to the UI

REST      = request/response API

Postgres  = persistent storage
```

---

# 8. WebSocket use #1: Live telemetry

One important purpose is live telemetry.

Suppose the spacecraft produces telemetry:

```text
Altitude
Velocity
Battery
Temperature
Fuel
Attitude
```

The information can move through the system:

```text
Physical Simulation
       |
       v
Telemetry
       |
       v
Kafka
       |
       v
State Processing
       |
       v
Redis
       |
       v
WebSocket
       |
       v
Mission Control UI
```

The UI can then display changing telemetry.

For example:

```text
Altitude: 1850 km
Velocity: 7.2 km/s
Battery: 82%
```

Then the values change:

```text
Altitude: 1860 km
Velocity: 7.1 km/s
Battery: 81.9%
```

The UI can receive those changes through the real-time path.

---

# 9. Why not repeatedly call REST?

Without a real-time connection, the UI could repeatedly ask:

```text
UI -> Backend:
"Give me the latest telemetry."

UI -> Backend:
"Give me the latest telemetry."

UI -> Backend:
"Give me the latest telemetry."

UI -> Backend:
"Give me the latest telemetry."
```

This is polling.

With WebSocket, the backend can send an update when appropriate:

```text
Backend
   |
   | telemetry update
   v
WebSocket
   |
   v
UI
```

This is useful for a live Mission Control interface.

---

# 10. WebSocket use #2: Mission state updates

TRISHULA also needs to represent changing mission state.

For example:

```text
MISSION
  |
  +-- Earth Orbit
  |
  +-- TLI
  |
  +-- Earth-Moon Transfer
  |
  +-- Moon Approach
  |
  +-- LOI
  |
  +-- Lunar Orbit
```

When mission state changes, the Mission Control UI needs to reflect the new state.

Conceptually:

```text
Mission state changes
        |
        v
Backend
        |
        v
WebSocket
        |
        v
Mission Control
```

The UI can therefore show the latest mission state.

---

# 11. WebSocket use #3: Command execution status

Another documented purpose is command execution status.

TRISHULA has a command path:

```text
Mission Control
       |
       v
Ground Command Service
       |
       v
Command Execution
       |
       v
Vehicle / Simulation
```

The command can move through different states.

For example:

```text
RECEIVED
   |
   v
VALIDATED
   |
   v
EXECUTING
   |
   v
COMPLETED
```

The Mission Control UI needs to know what happened.

WebSocket can provide the real-time status:

```text
Command
   |
   v
Execution
   |
   +----> Status update
             |
             v
          WebSocket
             |
             v
       Mission Control UI
```

So the operator can see command progress without repeatedly refreshing the page.

---

# 12. TRISHULA WebSocket roadmap

The project roadmap documents the WebSocket work in stages:

```text
V0.9.98
WebSocket Gateway

V0.9.99
Telemetry Streaming

V0.9.100
Command Execution WebSocket
```

This is important because WebSocket was not treated as one single feature.

It was developed as a sequence:

```text
WebSocket foundation
        |
        v
Telemetry streaming
        |
        v
Command execution updates
```

---

# 13. What is a WebSocket Gateway?

A gateway is a boundary through which clients connect to backend functionality.

Conceptually:

```text
Mission Control UI
        |
        | WebSocket connection
        v
+-------------------+
| WebSocket Gateway |
+-------------------+
        |
        v
Backend services
```

The gateway provides the real-time communication boundary between the UI and the backend system.

---

# 14. WebSocket and REST are different

This is a very important interview concept.

## REST

REST is generally request/response.

```text
UI
 |
 | GET /mission/state
 v
Backend
 |
 | JSON response
 v
UI
```

The UI asks for information.

## WebSocket

WebSocket keeps a communication connection open.

```text
UI
 <================>
      Backend
```

The backend can send updates through that connection.

---

# 15. Simple real-world example

Imagine a cricket scoreboard.

With REST polling:

```text
You:
"What is the score?"

Scoreboard:
"120"

A few seconds later:

You:
"What is the score?"

Scoreboard:
"121"
```

With WebSocket:

```text
Scoreboard
    |
    | "Score changed to 121"
    v
You
```

You do not have to repeatedly ask.

TRISHULA's Mission Control UI has a similar requirement for changing mission information.

---

# 16. WebSocket does NOT replace Kafka

This is very important.

Do not think:

```text
Kafka OR WebSocket
```

Think:

```text
Kafka + Redis + WebSocket
```

They have different jobs.

A simplified TRISHULA architecture is:

```text
C++ Simulation
      |
      | telemetry/events
      v
    Kafka
      |
      v
State Processor
      |
      v
    Redis
      |
      v
 WebSocket
      |
      v
Mission Control UI
```

---

# 17. WebSocket does NOT calculate the trajectory

Another important distinction:

WebSocket does not calculate:

```text
Gravity
Trajectory
TLI
LOI
Navigation
Guidance
Control
```

Those belong to the simulation, navigation, guidance, control, and mission-processing parts of TRISHULA.

WebSocket is a **communication mechanism**.

For example:

```text
Physics engine
     |
     | calculates state
     v
Telemetry/state
     |
     v
WebSocket
     |
     v
UI displays state
```

WebSocket transports information.

It does not create the physical information.

---

# 18. WebSocket does NOT make the system autonomous

Autonomy is a system capability.

WebSocket is communication infrastructure.

For example:

```text
Autonomy
   |
   +-- Navigation
   +-- Guidance
   +-- Planning
   +-- Decision logic
   +-- Control
```

WebSocket:

```text
Backend <========> UI
       real-time communication
```

So:

> **WebSocket helps Mission Control communicate in real time. It is not the autonomy system itself.**

---

# 19. Complete conceptual flow

Here is the easiest way to understand the complete relationship:

```text
              TRISHULA PHYSICAL SIDE
                     |
                     v
              C++ Simulation
                     |
                     | Physical state
                     v
                  Telemetry
                     |
                     v
                   Kafka
                     |
                     v
              State Processor
                     |
                     v
                   Redis
                     |
                     | Current state
                     v
              WebSocket Gateway
                     |
                     | Live updates
                     v
             Mission Control UI
```

And for commands:

```text
Mission Control UI
        |
        | Command
        v
Ground Command API
        |
        v
Command Execution
        |
        v
Vehicle / Simulation
        |
        v
Result / status
        |
        v
WebSocket
        |
        v
Mission Control UI
```

---

# 20. Why WebSocket is useful in Mission Control

Mission Control is not just a static webpage.

It is a live operational interface.

The operator may want to see:

```text
Current mission state
Current vehicle state
Current rover state
Telemetry
Events
Command status
Fault information
```

These values can change while the mission is running.

Therefore, a real-time communication mechanism is useful.

WebSocket provides that communication path.

---

# 21. Beginner mental model

Remember this:

```text
Kafka
"Move events/data inside the backend."

Redis
"Keep fast/current state."

WebSocket
"Push live updates to the UI."

REST
"Answer specific requests."

PostgreSQL
"Store persistent information."
```

---

# 22. Interview answer

If someone asks:

> "Why did you use WebSocket in TRISHULA?"

A good answer is:

> **"We use WebSocket as the real-time communication layer between the backend mission-state side and the Mission Control UI. It is useful for streaming live telemetry and mission-state changes to the UI, and for reflecting command execution status without relying on continuous REST polling. Kafka handles internal event transport, Redis holds fast current state, and WebSocket exposes real-time updates to Mission Control."**

---

# 23. One-line answer

If you need the shortest answer:

> **WebSocket is used in TRISHULA to keep Mission Control updated with live mission, telemetry, and command-execution information.**

---

# 24. Important source-boundary note

The project documentation and roadmap explicitly describe WebSocket Gateway, telemetry streaming, and command-execution WebSocket work at V0.9.98–V0.9.100.

This document therefore explains the **documented architecture and purpose**.

It should not be interpreted as proof that every WebSocket feature is currently active in every part of the V0.9.124.1 runtime. For an exact current source-code mapping, the V0.9.124.1 repository implementation should be inspected directly.

---

# 25. Final mental picture

The easiest way to remember everything:

```text
             C++ PHYSICAL SIMULATION
                       |
                       | telemetry
                       v
                     KAFKA
                       |
                       v
                STATE PROCESSOR
                       |
                       v
                     REDIS
                       |
                       | current state
                       v
                  WEBSOCKET
                       |
                       | live updates
                       v
              MISSION CONTROL UI
```

And in the other direction:

```text
MISSION CONTROL UI
        |
        | command
        v
GROUND COMMAND SERVICE
        |
        v
COMMAND EXECUTION
        |
        v
C++ / VEHICLE SIMULATION
        |
        v
NEW PHYSICAL STATE
        |
        v
TELEMETRY
        |
        v
WEBSOCKET
        |
        v
MISSION CONTROL UI
```

So the simplest sentence to remember is:

> **Kafka moves mission data inside the system, Redis keeps fast current state, WebSocket delivers live updates to Mission Control, and REST handles normal request/response operations.**
