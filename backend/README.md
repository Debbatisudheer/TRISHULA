# TRISHULA V0.9.55

Closed-Loop Mission Cycle.

Built additively from the verified/frozen V0.9.54 baseline.

## Purpose

V0.9.55 completes the first Ground Station & Closed Loop block by making the command → physical vehicle → event/telemetry feedback cycle explicit and ordered.

## Build

```bash
cmake -S . -B build_v055 -G "MinGW Makefiles"
cmake --build build_v055 -j2
ctest --test-dir build_v055 --output-on-failure
```

The generated build directory is intentionally not included in the source package.

## Baseline rule

V0.9.54 remains the frozen baseline. V0.9.55 adds only the closed-loop mission-cycle orchestration and regression coverage. No timer-driven mission progression, fake telemetry, or synthetic physical state is introduced.

## V0.9.105.1

V0.9.105.1 closes the rover/science checkpoint additively. It composes autonomous surface navigation, multi-target replanning, science acquisition, persistent science products, Rover -> Vikram -> Ground relay, ground science processing, and mission knowledge into a bounded autonomous surface-mission controller. No timer-driven mission progression or fake telemetry is introduced.
