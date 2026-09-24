package main

import (
	"testing"
	"time"
)

func buildHealthStateTestResult(source string, seq uint64, processedAt time.Time, attrs map[string]string, kind DataKind) ProcessingResult {
	return ProcessingResult{
		RecordID:           source + "-HS-" + string(kind),
		Kind:               kind,
		MissionID:          "TRISHULA",
		SourceNode:         source,
		SequenceNumber:     seq,
		MissionTimestampNS: seq * 1000,
		Status:             "accepted",
		Operation:          "health-state-test",
		ProcessorVersion:   currentProcessorVersion,
		ProcessedAt:        processedAt,
		Attributes:         attrs,
	}
}

func TestHealthStateNominalCombinesFreshGoodSubsystemNoFault(t *testing.T) {
	engine := NewMissionStateEngine()
	now := time.Unix(1000, 0).UTC()
	result := buildHealthStateTestResult("ROVER-01", 10, now.Add(-5*time.Second), map[string]string{
		"metric": "battery", "value": "90", "unit": "%", "quality_class": "GOOD",
		"limit_status": "NORMAL", "operational_state": "NOMINAL", "subsystem": "power",
	}, KindTelemetry)
	if err := engine.Apply(result); err != nil {
		t.Fatal(err)
	}
	state, ok, err := engine.HealthState("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected health state lookup: ok=%v err=%v", ok, err)
	}
	if state.HealthStatus != "NOMINAL" || state.Readiness != "READY" {
		t.Fatalf("unexpected aggregate: %+v", state)
	}
	vehicle := state.Vehicles[0]
	if vehicle.FreshnessStatus != "FRESH" || vehicle.QualityStatus != "GOOD" || vehicle.SubsystemStatus != "NOMINAL" || vehicle.FaultStatus != "NONE" {
		t.Fatalf("unexpected vehicle dimensions: %+v", vehicle)
	}
}

func TestHealthStateDegradedOnQualityOrStaleness(t *testing.T) {
	engine := NewMissionStateEngine()
	now := time.Unix(2000, 0).UTC()
	qualityResult := buildHealthStateTestResult("ROVER-01", 20, now.Add(-5*time.Second), map[string]string{
		"metric": "battery", "value": "25", "unit": "%", "quality_class": "DEGRADED",
		"limit_status": "LOW", "operational_state": "LOW_POWER", "subsystem": "power",
	}, KindTelemetry)
	if err := engine.Apply(qualityResult); err != nil {
		t.Fatal(err)
	}
	state, ok, err := engine.HealthState("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected health state lookup: ok=%v err=%v", ok, err)
	}
	if state.HealthStatus != "DEGRADED" || state.Readiness != "DEGRADED" {
		t.Fatalf("expected degraded health: %+v", state)
	}
	if state.DegradedMetricCount != 1 || state.Vehicles[0].QualityStatus != "DEGRADED" {
		t.Fatalf("unexpected quality aggregation: %+v", state)
	}

	staleEngine := NewMissionStateEngine()
	stale := buildHealthStateTestResult("ROVER-02", 20, now.Add(-45*time.Second), map[string]string{
		"metric": "battery", "value": "90", "unit": "%", "quality_class": "GOOD",
		"limit_status": "NORMAL", "operational_state": "NOMINAL", "subsystem": "power",
	}, KindTelemetry)
	if err := staleEngine.Apply(stale); err != nil {
		t.Fatal(err)
	}
	staleState, ok, err := staleEngine.HealthState("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected stale lookup: ok=%v err=%v", ok, err)
	}
	if staleState.HealthStatus != "DEGRADED" || staleState.Readiness != "DEGRADED" {
		t.Fatalf("expected stale vehicle to degrade health: %+v", staleState)
	}
	if staleState.StaleVehicleCount != 1 || staleState.Vehicles[0].FreshnessStatus != "STALE" {
		t.Fatalf("unexpected freshness aggregation: %+v", staleState)
	}
}

func TestHealthStateCriticalOnCriticalFaultOrVeryStale(t *testing.T) {
	engine := NewMissionStateEngine()
	now := time.Unix(3000, 0).UTC()
	fault := buildHealthStateTestResult("ROVER-01", 30, now.Add(-5*time.Second), map[string]string{
		"event_type": "ENGINE_FAULT", "severity": "CRITICAL", "subsystem": "propulsion",
		"active_fault_count": "1", "is_fault": "true", "lifecycle": "ACTIVE",
	}, KindEvent)
	if err := engine.Apply(fault); err != nil {
		t.Fatal(err)
	}
	state, ok, err := engine.HealthState("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected health state lookup: ok=%v err=%v", ok, err)
	}
	if state.HealthStatus != "CRITICAL" || state.Readiness != "NOT_READY" {
		t.Fatalf("expected critical fault to be not-ready: %+v", state)
	}
	if state.HighestFaultSeverity != "CRITICAL" || state.Vehicles[0].FaultStatus != "CRITICAL" {
		t.Fatalf("unexpected fault aggregation: %+v", state)
	}

	staleEngine := NewMissionStateEngine()
	veryStale := buildHealthStateTestResult("ROVER-02", 30, now.Add(-3*time.Minute), map[string]string{
		"metric": "battery", "value": "90", "unit": "%", "quality_class": "GOOD",
		"limit_status": "NORMAL", "operational_state": "NOMINAL", "subsystem": "power",
	}, KindTelemetry)
	if err := staleEngine.Apply(veryStale); err != nil {
		t.Fatal(err)
	}
	staleState, ok, err := staleEngine.HealthState("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected stale lookup: ok=%v err=%v", ok, err)
	}
	if staleState.HealthStatus != "CRITICAL" || staleState.Readiness != "NOT_READY" {
		t.Fatalf("expected very stale vehicle to be critical: %+v", staleState)
	}
	if staleState.VeryStaleVehicleCount != 1 {
		t.Fatalf("unexpected very stale count: %+v", staleState)
	}
}

func TestHealthStateUnknownWithoutMission(t *testing.T) {
	engine := NewMissionStateEngine()
	_, ok, err := engine.HealthState("TRISHULA", time.Unix(1000, 0).UTC(), 30*time.Second, 2*time.Minute)
	if err != nil {
		t.Fatal(err)
	}
	if ok {
		t.Fatal("expected absent mission health state to be not found")
	}
}

func TestHealthStateMissionAggregatesMultipleVehiclesDeterministically(t *testing.T) {
	engine := NewMissionStateEngine()
	now := time.Unix(4000, 0).UTC()
	inputs := []ProcessingResult{
		buildHealthStateTestResult("ROVER-02", 20, now.Add(-5*time.Second), map[string]string{
			"metric": "battery", "value": "90", "unit": "%", "quality_class": "GOOD",
			"limit_status": "NORMAL", "operational_state": "NOMINAL", "subsystem": "power",
		}, KindTelemetry),
		buildHealthStateTestResult("ROVER-01", 20, now.Add(-5*time.Second), map[string]string{
			"metric": "temperature", "value": "340", "unit": "K", "quality_class": "GOOD",
			"limit_status": "HIGH", "operational_state": "HOT", "subsystem": "thermal",
		}, KindTelemetry),
	}
	for _, input := range inputs {
		if err := engine.Apply(input); err != nil {
			t.Fatal(err)
		}
	}
	state, ok, err := engine.HealthState("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected health state lookup: ok=%v err=%v", ok, err)
	}
	if state.HealthStatus != "DEGRADED" || state.DegradedVehicleCount != 1 || state.NominalVehicleCount != 1 {
		t.Fatalf("unexpected mission aggregation: %+v", state)
	}
	if len(state.Vehicles) != 2 || state.Vehicles[0].SourceNode != "ROVER-01" || state.Vehicles[1].SourceNode != "ROVER-02" {
		t.Fatalf("unexpected vehicle ordering: %+v", state.Vehicles)
	}
}
