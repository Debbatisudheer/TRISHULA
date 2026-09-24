package main

import "testing"

func TestMissionHealthNominalReady(t *testing.T) {
	engine := NewMissionStateEngine()
	result := buildSubsystemResult("H-1", 10, KindTelemetry, map[string]string{
		"metric": "battery", "value": "90", "unit": "%", "quality_class": "GOOD", "operational_state": "NOMINAL",
	})
	if err := engine.Apply(result); err != nil {
		t.Fatal(err)
	}
	health, ok := engine.Health("TRISHULA")
	if !ok {
		t.Fatal("mission health not found")
	}
	if health.HealthStatus != "NOMINAL" || health.Readiness != "READY" {
		t.Fatalf("unexpected health: %+v", health)
	}
	if health.NominalVehicleCount != 1 || health.NominalSubsystemCount != 1 {
		t.Fatalf("unexpected counts: %+v", health)
	}
}

func TestMissionHealthDegradedOnVehicleOrSubsystemWarning(t *testing.T) {
	engine := NewMissionStateEngine()
	result := buildSubsystemResult("H-2", 20, KindTelemetry, map[string]string{
		"metric": "temperature", "value": "340", "unit": "K", "quality_class": "GOOD", "limit_status": "HIGH", "operational_state": "HOT",
	})
	if err := engine.Apply(result); err != nil {
		t.Fatal(err)
	}
	health, ok := engine.Health("TRISHULA")
	if !ok {
		t.Fatal("mission health not found")
	}
	if health.HealthStatus != "DEGRADED" || health.Readiness != "DEGRADED" {
		t.Fatalf("unexpected health: %+v", health)
	}
	if health.DegradedVehicleCount != 1 || health.DegradedSubsystemCount != 1 {
		t.Fatalf("unexpected counts: %+v", health)
	}
}

func TestMissionHealthCriticalAndNotReadyOnCriticalFault(t *testing.T) {
	engine := NewMissionStateEngine()
	result := buildSubsystemResult("H-3", 30, KindEvent, map[string]string{
		"event_type": "ENGINE_FAULT", "severity": "CRITICAL", "subsystem": "propulsion", "active_fault_count": "1", "is_fault": "true", "lifecycle": "ACTIVE",
	})
	if err := engine.Apply(result); err != nil {
		t.Fatal(err)
	}
	health, ok := engine.Health("TRISHULA")
	if !ok {
		t.Fatal("mission health not found")
	}
	if health.HealthStatus != "CRITICAL" || health.Readiness != "NOT_READY" {
		t.Fatalf("unexpected health: %+v", health)
	}
	if health.CriticalVehicleCount != 1 || health.CriticalSubsystemCount != 1 {
		t.Fatalf("unexpected counts: %+v", health)
	}
}

func TestMissionHealthUnknownWhenNoVehicles(t *testing.T) {
	engine := NewMissionStateEngine()
	health, ok := engine.Health("TRISHULA")
	if ok {
		t.Fatal("expected unknown mission health for absent mission")
	}
	if health.HealthStatus != "" {
		t.Fatalf("unexpected zero-value health: %+v", health)
	}
}

func TestMissionHealthDeterministicVehicleOrdering(t *testing.T) {
	engine := NewMissionStateEngine()
	for _, source := range []string{"ROVER-02", "ROVER-01"} {
		result := buildSubsystemResult("H-"+source, 10, KindTelemetry, map[string]string{
			"metric": "battery", "value": "90", "unit": "%", "quality_class": "GOOD", "operational_state": "NOMINAL",
		})
		result.SourceNode = source
		if err := engine.Apply(result); err != nil {
			t.Fatal(err)
		}
	}
	health, ok := engine.Health("TRISHULA")
	if !ok {
		t.Fatal("mission health not found")
	}
	if len(health.Vehicles) != 2 {
		t.Fatalf("expected 2 vehicles, got %d", len(health.Vehicles))
	}
	if health.Vehicles[0].SourceNode != "ROVER-01" || health.Vehicles[1].SourceNode != "ROVER-02" {
		t.Fatalf("unexpected ordering: %+v", health.Vehicles)
	}
}
