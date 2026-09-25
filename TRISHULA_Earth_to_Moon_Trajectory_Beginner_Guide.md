# TRISHULA: How the Spacecraft Trajectory to the Moon Works

## Beginner-Friendly Explanation

This document explains one important question:

> **How does TRISHULA calculate and follow the spacecraft's trajectory from Earth to the Moon?**

The explanation is intentionally simple so that someone who is new to space missions, physics, or software can understand it.

---

# 1. What is a trajectory?

A **trajectory** is the path that a spacecraft follows through space over time.

Think about throwing a ball.

The ball does not stay in one place. It moves through different positions:

```text
Your hand
   |
   v
   * -----> * -----> * -----> *
                              |
                              v
                           Target
```

The series of positions of the ball makes its path.

A spacecraft works in the same basic way, but the calculation is much more complex because:

- Earth has gravity.
- The Moon has gravity.
- The spacecraft is moving very fast.
- The Moon is also moving.
- The spacecraft can change its velocity using its engine.
- Small errors can change the future path.

So:

> **Trajectory = where the spacecraft is expected to be at different times.**

---

# 2. Does TRISHULA already have the whole path stored?

Not simply as a fixed line.

This is very important.

A spacecraft trajectory is generally **calculated and propagated from the spacecraft's state, target, physics, and mission requirements**.

For example, TRISHULA starts with information such as:

```text
Spacecraft position
Spacecraft velocity
Spacecraft mass
Spacecraft attitude
Fuel / propulsion state
Moon position
Moon velocity
Earth gravity
Moon gravity
Mission target
```

The physics system then uses this information to calculate how the spacecraft will move.

So we can think of it like:

```text
Current State
     +
Moon State
     +
Physics
     +
Mission Target
     +
Mission Constraints
     |
     v
Trajectory Calculation
```

The result is a predicted/reference path.

---

# 3. Why can't the spacecraft simply aim at the Moon?

Because the Moon is moving.

Imagine you want to throw a ball to a person who is running.

If you throw the ball at where the person is **right now**, the person may already be somewhere else when the ball arrives.

Instead, you need to aim toward where the person is expected to be in the future.

The same idea applies to the Moon.

```text
Current Moon position
        O

             \
              \
               \   Spacecraft trajectory
                \
                 \
                  O  Future Moon position
```

The spacecraft therefore needs to consider the Moon's **future position**, not just its current position.

---

# 4. What information does the trajectory calculation use?

At a simple conceptual level, TRISHULA needs several kinds of information.

## Spacecraft state

The spacecraft state describes where the spacecraft is and how it is moving.

For example:

```text
Position
Velocity
Mass
Attitude
Angular velocity
Propulsion state
```

## Moon state

The Moon is also a moving object.

The simulation therefore needs information such as:

```text
Moon position
Moon velocity
Moon-relative geometry
```

## Physics

The physics model determines how the spacecraft changes over time.

Important effects include:

```text
Gravity
Spacecraft dynamics
Propulsion
Attitude dynamics
Other modeled perturbations
```

## Mission target

The mission tells the system what it is trying to accomplish.

For example:

```text
Leave Earth orbit
Reach the Moon
Enter lunar orbit
Perform descent
Land
```

---

# 5. The basic trajectory idea

At a very high level:

```text
Spacecraft State
       |
       v
Physics Model
       |
       v
Predict Future State
       |
       v
Compare With Target
       |
       v
Calculate / Update Trajectory
```

The important idea is that the system repeatedly predicts where the spacecraft will go.

---

# 6. What is TLI?

TLI means:

> **Trans-Lunar Injection**

It is the major maneuver that places the spacecraft onto its Earth-to-Moon transfer.

Before TLI, the spacecraft is in an Earth-orbiting state.

Conceptually:

```text
             Moon
              O
             /
            /
           /
          /
      ----*----------------
        Earth
```

The TLI burn changes the spacecraft's velocity.

That velocity change puts the spacecraft onto a trajectory that can take it from the Earth region toward the Moon.

---

# 7. Why is velocity so important?

Velocity is not only about how fast something moves.

It also has a direction.

For example:

```text
Velocity
   |
   +-------> speed + direction
```

Changing velocity changes the future path.

A small velocity change can eventually produce a large position difference.

For example:

```text
Small velocity change
        |
        v
Different trajectory
        |
        v
Large position difference later
```

That is why spacecraft maneuvers are carefully calculated.

---

# 8. What happens after TLI?

After the TLI maneuver, the spacecraft does not necessarily keep firing its engine.

It can enter a **coast phase**.

During the coast:

```text
Engine burn
    |
    v
New velocity
    |
    v
Coast
    |
    v
Gravity + physics
    |
    v
New position and velocity
```

The physics engine continues calculating the spacecraft's state.

At each simulation step, the system can calculate:

```text
Position
Velocity
Acceleration
```

Then it advances the spacecraft to the next time step.

---

# 9. What does "trajectory propagation" mean?

This is a very important term.

**Trajectory propagation** means:

> Starting from a known state and using the physics model to calculate where the spacecraft will be in the future.

For example:

```text
Time 0
Position = P0
Velocity = V0

       |
       | Physics calculation
       v

Time 1
Position = P1
Velocity = V1

       |
       | Physics calculation
       v

Time 2
Position = P2
Velocity = V2

       |
       v

Time 3
Position = P3
Velocity = V3
```

Connecting these positions gives the predicted trajectory.

So:

```text
State at time 0
      |
      v
Physics
      |
      v
State at time 1
      |
      v
Physics
      |
      v
State at time 2
      |
      v
...
```

That is trajectory propagation.

---

# 10. Why does TRISHULA keep calculating instead of calculating only once?

Because the spacecraft's state changes continuously.

Also, the predicted state can differ from the desired state.

So the system needs to repeatedly check:

```text
Where am I now?
        |
        v
Where am I expected to be?
        |
        v
Is the difference acceptable?
```

This creates a feedback process.

```text
Calculate
   |
   v
Propagate
   |
   v
Check state
   |
   v
Compare
   |
   v
Correct if required
   |
   v
Propagate again
```

---

# 11. What is an MCC?

MCC means:

> **Mid-Course Correction**

It is a correction maneuver performed during the transfer when a trajectory adjustment is needed.

Imagine the spacecraft is moving toward the Moon:

```text
Expected path
-----------------------------> Moon

Actual path
-------------------------->
                         \
                          \
                           \

Correction
     *
    /
   /
```

A correction burn changes the spacecraft's velocity.

That changes the future trajectory.

So:

```text
Current trajectory
       |
       v
Find deviation
       |
       v
Correction burn
       |
       v
New velocity
       |
       v
New trajectory
```

MCC is therefore not "turning the spacecraft toward the Moon" like a car turning on a road.

It is a controlled change in velocity that changes the spacecraft's future path.

---

# 12. Does the spacecraft continuously compare itself with the Moon?

Conceptually, the navigation and trajectory systems use the spacecraft's state and the Moon's state to determine the spacecraft's relationship to the target.

The system can reason about:

```text
Where is the spacecraft?
Where is the Moon?
How fast is each moving?
Where is the spacecraft expected to go?
What is the desired mission state?
```

This is especially important as the spacecraft gets closer to the Moon.

---

# 13. What happens near the Moon?

As the spacecraft approaches the Moon, the mission transitions from primarily Earth-centered thinking toward **lunar-relative navigation** and lunar operations.

Conceptually:

```text
Earth-centered transfer
        |
        v
Moon approach
        |
        v
Lunar-relative navigation
        |
        v
LOI targeting
        |
        v
LOI burn
        |
        v
Lunar orbit
```

---

# 14. What is LOI?

LOI means:

> **Lunar Orbit Insertion**

The spacecraft approaches the Moon on its transfer trajectory.

It then performs a maneuver that changes its velocity so that it can enter a lunar orbit.

Conceptually:

```text
Earth
  |
  | TLI
  v
Earth-Moon transfer
  |
  v
Moon approach
  |
  | LOI burn
  v
Lunar orbit
```

---

# 15. Complete Earth-to-Moon flow

A simplified TRISHULA mission flow looks like this:

```text
Earth Orbit
    |
    v
Orbit Verification
    |
    v
Orbit Determination
    |
    v
Trajectory Propagation
    |
    v
TLI Targeting
    |
    v
TLI Preparation
    |
    v
TLI Burn
    |
    v
Earth-to-Moon Transfer
    |
    v
Coast
    |
    v
Navigation
    |
    v
Trajectory Prediction
    |
    v
Error / Deviation Check
    |
    +---- If acceptable ----> Continue Coast
    |
    +---- If correction needed
                    |
                    v
                 MCC
                    |
                    v
             Continue Transfer
                    |
                    v
             Moon Approach
                    |
                    v
        Lunar-Relative Navigation
                    |
                    v
              LOI Targeting
                    |
                    v
                 LOI Burn
                    |
                    v
              Lunar Orbit
```

---

# 16. The most important idea: trajectory is not just a line

A beginner may imagine a trajectory like this:

```text
Earth ---------------------- Moon
             one fixed line
```

But a real simulation thinks about the spacecraft's state over time.

A more useful mental model is:

```text
Time 0      Time 1      Time 2      Time 3      Time 4

  * ---------- * ---------- * ---------- * ---------- *
State         State        State        State        State
  |             |            |            |            |
  v             v            v            v            v
Physics       Physics      Physics      Physics      Physics
```

Each state contains information about the spacecraft.

Therefore:

> **Trajectory = the evolution of the spacecraft's state over time.**

---

# 17. Planned trajectory vs actual/predicted trajectory

These terms can be confusing.

## Planned / reference trajectory

This is the trajectory the mission intends to follow.

Example:

```text
Desired path
     |
     v
Earth ------------------> Moon
```

## Propagated / predicted trajectory

This is what the physics model predicts from the current state.

```text
Current state
     |
     v
Physics model
     |
     v
Predicted future path
```

## Current state

This is where the simulation currently says the spacecraft is.

```text
Current position
Current velocity
Current attitude
Current mass
...
```

The system uses these together.

```text
Reference trajectory
        +
Current state
        +
Physics
        |
        v
Prediction / guidance / correction
```

---

# 18. A very simple example

Imagine you are throwing a ball to a moving friend.

Your friend is here:

```text
Friend now
   O
```

But while the ball is flying, your friend moves:

```text
Friend now                  Friend later
   O ---------------------------> O
```

If you throw directly at the first position, you may miss.

Instead, you estimate where your friend will be when the ball arrives.

That is similar to the basic idea of targeting the Moon.

For TRISHULA:

```text
Spacecraft state
       +
Moon future state
       +
Physics
       |
       v
Transfer trajectory
```

---

# 19. A slightly more technical view

A spacecraft state can be thought of as a collection of values such as:

```text
State =
[
    position,
    velocity,
    attitude,
    angular velocity,
    mass,
    ...
]
```

The physics engine calculates how that state changes with time.

Conceptually:

```text
Current state
     |
     v
Calculate forces / dynamics
     |
     v
Calculate acceleration
     |
     v
Integrate over time
     |
     v
New state
```

The process repeats.

This is the mathematical foundation behind trajectory propagation.

The exact numerical equations and algorithms depend on the implementation.

---

# 20. Where does guidance fit?

These terms are related but not identical.

## Navigation

Navigation answers:

> "Where am I?"

It estimates the spacecraft's current state.

## Guidance

Guidance answers:

> "Where should I go and what should I do to get there?"

It uses the desired trajectory or mission objective.

## Control

Control answers:

> "How do I physically make the spacecraft do that?"

For example, control may determine how to apply thrust or attitude commands.

So the simplified relationship is:

```text
Sensors / State
      |
      v
Navigation
"Where am I?"
      |
      v
Guidance
"Where should I go?"
      |
      v
Control
"How do I make it happen?"
      |
      v
Physical action
      |
      v
New state
      |
      +--------------------+
                           |
                           v
                      Navigation
```

This is a closed loop.

---

# 21. The complete closed loop

This is one of the most important concepts in TRISHULA.

```text
        +----------------------+
        |      SPACECRAFT      |
        +----------------------+
                   |
                   | sensors / state
                   v
        +----------------------+
        |     NAVIGATION       |
        +----------------------+
                   |
                   | estimated state
                   v
        +----------------------+
        |      GUIDANCE        |
        +----------------------+
                   |
                   | desired action
                   v
        +----------------------+
        |       CONTROL        |
        +----------------------+
                   |
                   | actuator / thrust
                   v
        +----------------------+
        |     PHYSICAL MODEL   |
        +----------------------+
                   |
                   | new physical state
                   v
        +----------------------+
        |     SENSORS / STATE  |
        +----------------------+
                   |
                   +-----------> repeat
```

In TRISHULA, this is simulated in software.

---

# 22. How the Moon fits into the loop

The Moon is not just a destination marker.

The Moon itself has a modeled state.

Conceptually:

```text
Earth state
    |
    v
Spacecraft state ------> Physics ------> Future spacecraft state
    |
    |
    +------ Moon state
               |
               v
        Target / relative geometry
```

The spacecraft trajectory is therefore calculated in a moving gravitational environment rather than toward a stationary picture of the Moon.

---

# 23. What happens if the spacecraft is exactly on trajectory?

If the predicted state is within the required limits, the system can continue with the current plan.

Conceptually:

```text
Predicted state
      |
      v
Compare
      |
      v
Within limits?
   /       \
 YES       NO
  |         |
  v         v
Continue   Correction
```

The exact limits and decision logic depend on the mission and implementation.

---

# 24. What happens if it is not on trajectory?

If the deviation is significant enough to require action, a correction can be planned.

Conceptually:

```text
Deviation detected
       |
       v
Calculate correction
       |
       v
Change velocity
       |
       v
New trajectory
       |
       v
Propagate again
       |
       v
Check again
```

This is why spacecraft navigation and guidance are continuous processes rather than one-time calculations.

---

# 25. Is TRISHULA doing this with a single trajectory calculation?

No.

The important mental model is:

```text
Initial state
     |
     v
Initial trajectory / target
     |
     v
Simulation
     |
     v
New state
     |
     v
Prediction
     |
     v
Check
     |
     v
Correction if required
     |
     v
Continue
```

The trajectory evolves with the spacecraft state.

---

# 26. Where does the physics engine come in?

The physics engine is responsible for modeling how the spacecraft moves.

For example, conceptually:

```text
Gravity
   +
Propulsion
   +
Vehicle dynamics
   +
Perturbations
   |
   v
Acceleration / motion
   |
   v
New velocity
   |
   v
New position
```

The exact implementation should be understood from the actual TRISHULA source code rather than assuming a particular equation or numerical method.

---

# 27. Does TRISHULA use AI to calculate the Moon trajectory?

The basic trajectory and physics process should not be confused with AI.

Trajectory propagation is fundamentally a physics and numerical-computation problem.

A simplified architecture is:

```text
Physics
   +
Navigation
   +
Guidance
   +
Control
   |
   v
Autonomous mission behavior
```

AI/ML can be used in other parts of an autonomous system, such as:

```text
Image understanding
Terrain classification
Hazard detection
Anomaly detection
Prediction
```

But a system does not need AI to calculate a physical trajectory.

This is an important distinction:

> **AI can support autonomy, but physics-based trajectory propagation is not automatically AI.**

---

# 28. Where autonomy fits

A human does not have to manually control every tiny motion.

The high-level mission can provide a goal:

```text
Human:
"Reach lunar orbit."
```

The onboard/autonomous system can then perform local steps such as:

```text
Navigate
   |
   v
Check state
   |
   v
Follow guidance
   |
   v
Control
   |
   v
Execute action
   |
   v
Check result
   |
   v
Continue
```

This is autonomy.

A simple distinction:

```text
AI       = a technology that can help understand, predict, classify, etc.

Autonomy = the ability of a system to decide and act within defined rules.

Control  = makes the physical system follow the desired behavior.

Physics  = models how the physical system behaves.
```

---

# 29. Beginner mental model

If you remember only five things, remember these:

### 1. Trajectory

> The spacecraft's path through space over time.

### 2. Propagation

> Use physics to predict how the spacecraft's state changes over time.

### 3. Navigation

> Determine where the spacecraft is.

### 4. Guidance

> Determine where the spacecraft should go.

### 5. Control

> Make the spacecraft physically follow the desired behavior.

Together:

```text
Where am I?
    |
    v
Navigation
    |
    v
Where should I go?
    |
    v
Guidance
    |
    v
How do I get there?
    |
    v
Control
    |
    v
Physical movement
    |
    v
Where am I now?
    |
    +------ repeat
```

---

# 30. TRISHULA Earth-to-Moon trajectory in one picture

```text
                         MOON
                          O
                         / \
                        /   \   Lunar Orbit
                       /     \______
                      /
                     /   LOI
                    /
                   /
                  /
                 /
                /
               /
              /
             /
            *
        SPACECRAFT
            |
            |
            | Earth-to-Moon transfer
            |
      ______|_______
     /              \
    /      EARTH     \
   |        O         |
    \                /
     \______________/

          ^
          |
         TLI
```

The important thing is not the drawing itself.

The important process is:

```text
Earth orbit
   ↓
Know spacecraft state
   ↓
Know Moon state
   ↓
Calculate/propagate transfer
   ↓
TLI burn
   ↓
Coast under modeled physics
   ↓
Navigate and predict
   ↓
Check trajectory
   ↓
MCC if required
   ↓
Approach Moon
   ↓
Lunar-relative navigation
   ↓
LOI burn
   ↓
Lunar orbit
```

---

# 31. Interview answer

If someone asks:

> "How does TRISHULA generate and follow the trajectory to the Moon?"

A good simple answer is:

> **"TRISHULA starts with the spacecraft's current state and the Moon's modeled state. It uses the physics model to propagate the spacecraft's motion and determine an Earth-to-Moon transfer trajectory. TLI changes the spacecraft's velocity and places it on the transfer path. During the transfer, the system continuously propagates the state, performs navigation and trajectory checks, and can model correction maneuvers such as MCC when required. As the spacecraft approaches the Moon, the system transitions to lunar-relative navigation and LOI targeting, followed by Lunar Orbit Insertion."**

---

# 32. One-line explanation

If you need the shortest possible explanation:

> **TRISHULA calculates where the spacecraft should go, uses physics to predict where it will actually go, checks the difference, applies corrections when needed, and finally guides the spacecraft into lunar orbit.**

---

# 33. Final mental picture

Remember this:

```text
             MISSION GOAL
                  |
                  v
          +---------------+
          |    TARGET     |
          |     MOON      |
          +---------------+
                  |
                  v
          +---------------+
          |   TRAJECTORY  |
          |    PLANNING   |
          +---------------+
                  |
                  v
          +---------------+
          |    PHYSICS    |
          |  PROPAGATION  |
          +---------------+
                  |
                  v
          +---------------+
          |   SPACECRAFT  |
          |    MOTION     |
          +---------------+
                  |
                  v
          +---------------+
          |  NAVIGATION   |
          | "WHERE AM I?" |
          +---------------+
                  |
                  v
          +---------------+
          |   GUIDANCE    |
          | "WHERE NEXT?" |
          +---------------+
                  |
                  v
          +---------------+
          |    CONTROL    |
          | "MAKE IT MOVE"|
          +---------------+
                  |
                  v
          +---------------+
          |   NEW STATE   |
          +---------------+
                  |
                  +---------> REPEAT
```

That repeated loop is the core idea behind understanding how TRISHULA can model an Earth-to-Moon mission.
