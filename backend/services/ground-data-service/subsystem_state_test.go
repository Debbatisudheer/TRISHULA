package main

import (
	"testing"
	"time"
)

func buildSubsystemResult(id string, seq uint64, kind DataKind, attrs map[string]string) ProcessingResult {
	return ProcessingResult{
		RecordID:           id,
		Kind:               kind,
		MissionID:          "TRISHULA",
		SourceNode:         "ROVER-01",
		SequenceNumber:     seq,
		MissionTimestampNS: seq * 1000,
		Status:             "accepted",
		Operation:          "test:" + string(kind),
		ProcessorVersion:   currentProcessorVersion,
		ProcessedAt:        time.Unix(0, int64(seq)*1_000_000).UTC(),
		Attributes:         attrs,
	}
}

func TestSubsystemStateAggregationSeparatesTelemetryDomains(t *testing.T) {
	engine := NewMissionStateEngine()
	battery := buildSubsystemResult("SUB-P-1", 10, KindTelemetry, map[string]string{
		"metric": "battery", "value": "84", "unit": "%", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	temperature := buildSubsystemResult("SUB-T-1", 11, KindTelemetry, map[string]string{
		"metric": "temperature", "value": "340", "unit": "K", "quality_class": "GOOD", "limit_status": "HIGH", "operational_state": "HOT",
	})
	if err := engine.Apply(battery); err != nil {
		t.Fatal(err)
	}
	if err := engine.Apply(temperature); err != nil {
		t.Fatal(err)
	}

	subs := engine.SubsystemSummaries("TRISHULA", "ROVER-01")
	if len(subs) != 2 {
		t.Fatalf("expected 2 subsystems, got %d: %+v", len(subs), subs)
	}
	if subs[0].Name != "power" || subs[1].Name != "thermal" {
		t.Fatalf("unexpected subsystem order: %+v", subs)
	}
	if subs[0].HealthStatus != "NOMINAL" {
		t.Fatalf("power health: %s", subs[0].HealthStatus)
	}
	if subs[1].HealthStatus != "DEGRADED" {
		t.Fatalf("thermal health: %s", subs[1].HealthStatus)
	}
	if subs[1].Metrics["temperature"].OperationalState != "HOT" {
		t.Fatalf("thermal metric missing: %+v", subs[1])
	}
}

func TestSubsystemStateUsesExplicitSubsystemFromEvent(t *testing.T) {
	engine := NewMissionStateEngine()
	event := buildSubsystemResult("SUB-E-1", 20, KindEvent, map[string]string{
		"event_type": "THERMAL_WARNING", "severity": "WARNING", "subsystem": "thermal", "active_fault_count": "1", "is_fault": "true",
	})
	if err := engine.Apply(event); err != nil {
		t.Fatal(err)
	}
	state, ok := engine.Subsystem("TRISHULA", "ROVER-01", "thermal")
	if !ok {
		t.Fatal("thermal subsystem not found")
	}
	if state.ActiveFaultCount != 1 || state.HighestFaultSeverity != "WARNING" || state.LastEventType != "THERMAL_WARNING" {
		t.Fatalf("unexpected thermal state: %+v", state)
	}
	if state.HealthStatus != "DEGRADED" {
		t.Fatalf("expected degraded subsystem health, got %s", state.HealthStatus)
	}
}

func TestSubsystemStateDoesNotRegressIndependently(t *testing.T) {
	engine := NewMissionStateEngine()
	newer := buildSubsystemResult("SUB-N", 50, KindTelemetry, map[string]string{
		"metric": "battery", "value": "80", "unit": "%", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	older := buildSubsystemResult("SUB-O", 49, KindTelemetry, map[string]string{
		"metric": "battery", "value": "20", "unit": "%", "quality_class": "DEGRADED", "limit_status": "LOW", "operational_state": "LOW_POWER",
	})
	if err := engine.Apply(newer); err != nil {
		t.Fatal(err)
	}
	if err := engine.Apply(older); err != nil {
		t.Fatal(err)
	}
	state, ok := engine.Subsystem("TRISHULA", "ROVER-01", "power")
	if !ok {
		t.Fatal("power subsystem not found")
	}
	if state.LastSequence != 50 || state.Metrics["battery"].Value != "80" || state.HealthStatus != "NOMINAL" {
		t.Fatalf("subsystem regressed: %+v", state)
	}
}

func TestSubsystemStateCanAcceptOlderThanVehicleGlobalSequence(t *testing.T) {
	engine := NewMissionStateEngine()
	mobility := buildSubsystemResult("MOB-20", 20, KindTelemetry, map[string]string{
		"metric": "speed", "value": "1.2", "unit": "m/s", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	power := buildSubsystemResult("PWR-10", 10, KindTelemetry, map[string]string{
		"metric": "battery", "value": "91", "unit": "%", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	if err := engine.Apply(mobility); err != nil {
		t.Fatal(err)
	}
	if err := engine.Apply(power); err != nil {
		t.Fatal(err)
	}
	speedState, ok := engine.Subsystem("TRISHULA", "ROVER-01", "mobility")
	if !ok || speedState.LastSequence != 20 {
		t.Fatalf("mobility subsystem changed unexpectedly: %+v", speedState)
	}
	powerState, ok := engine.Subsystem("TRISHULA", "ROVER-01", "power")
	if !ok || powerState.LastSequence != 10 {
		t.Fatalf("power subsystem should accept independent sequence: %+v", powerState)
	}
}
