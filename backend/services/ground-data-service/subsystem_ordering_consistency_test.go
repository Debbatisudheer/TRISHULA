package main

import "testing"

// TestSubsystemOrderingInvariant verifies the ordering rule before we advance
// to the next mission-state milestone:
//  1. Vehicle-wide state never regresses on an older source sequence.
//  2. Each subsystem maintains its own monotonic sequence boundary.
//  3. A subsystem may legitimately lag the vehicle-wide latest sequence when
//     its own latest observation is older than another subsystem's observation.
//  4. A late packet for an already-populated subsystem must not regress it.
func TestSubsystemOrderingInvariant(t *testing.T) {
	engine := NewMissionStateEngine()

	mobility20 := buildSubsystemResult("MOB-20", 20, KindTelemetry, map[string]string{
		"metric": "speed", "value": "1.2", "unit": "m/s",
		"quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	power10 := buildSubsystemResult("PWR-10", 10, KindTelemetry, map[string]string{
		"metric": "battery", "value": "91", "unit": "%",
		"quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	power09 := buildSubsystemResult("PWR-09", 9, KindTelemetry, map[string]string{
		"metric": "battery", "value": "40", "unit": "%",
		"quality_class": "DEGRADED", "limit_status": "LOW", "operational_state": "LOW_POWER",
	})
	mobility19 := buildSubsystemResult("MOB-19", 19, KindTelemetry, map[string]string{
		"metric": "speed", "value": "0.4", "unit": "m/s",
		"quality_class": "DEGRADED", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})

	for _, result := range []ProcessingResult{mobility20, power10, power09, mobility19} {
		if err := engine.Apply(result); err != nil {
			t.Fatalf("apply %s: %v", result.RecordID, err)
		}
	}

	vehicle, ok := engine.Vehicle("TRISHULA", "ROVER-01")
	if !ok {
		t.Fatal("vehicle state not found")
	}
	if vehicle.LastSequence != 20 {
		t.Fatalf("vehicle global state regressed/changed unexpectedly: got sequence %d", vehicle.LastSequence)
	}

	mobility, ok := engine.Subsystem("TRISHULA", "ROVER-01", "mobility")
	if !ok {
		t.Fatal("mobility subsystem not found")
	}
	if mobility.LastSequence != 20 || mobility.Metrics["speed"].Value != "1.2" {
		t.Fatalf("mobility subsystem regressed: %+v", mobility)
	}

	power, ok := engine.Subsystem("TRISHULA", "ROVER-01", "power")
	if !ok {
		t.Fatal("power subsystem not found")
	}
	if power.LastSequence != 10 || power.Metrics["battery"].Value != "91" {
		t.Fatalf("power subsystem accepted a regression: %+v", power)
	}

	// This is the key rule: the power subsystem can legitimately remain at
	// sequence 10 even though another subsystem has vehicle-wide sequence 20.
	if power.LastSequence >= vehicle.LastSequence {
		t.Fatalf("expected subsystem to lag vehicle-wide sequence; subsystem=%d vehicle=%d", power.LastSequence, vehicle.LastSequence)
	}
}
