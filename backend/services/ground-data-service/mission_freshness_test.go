package main

import (
	"testing"
	"time"
)

func buildFreshnessTestResult(source string, seq uint64, processedAt time.Time, subsystem, metric, opState string) ProcessingResult {
	attrs := map[string]string{
		"metric":            metric,
		"value":             "90",
		"unit":              "%",
		"quality_class":     "GOOD",
		"limit_status":      "NORMAL",
		"operational_state": opState,
		"subsystem":         subsystem,
	}
	return ProcessingResult{
		RecordID:           source + "-" + metric,
		Kind:               KindTelemetry,
		MissionID:          "TRISHULA",
		SourceNode:         source,
		SequenceNumber:     seq,
		MissionTimestampNS: seq * 1000,
		Status:             "accepted",
		Operation:          "freshness-test",
		ProcessorVersion:   currentProcessorVersion,
		ProcessedAt:        processedAt,
		Attributes:         attrs,
	}
}

func TestMissionFreshnessFreshVehicle(t *testing.T) {
	engine := NewMissionStateEngine()
	now := time.Unix(1000, 0).UTC()
	if err := engine.Apply(buildFreshnessTestResult("ROVER-01", 10, now.Add(-5*time.Second), "power", "battery", "NOMINAL")); err != nil {
		t.Fatal(err)
	}
	summary, ok, err := engine.Freshness("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected freshness lookup: ok=%v err=%v", ok, err)
	}
	if summary.FreshnessStatus != "FRESH" || summary.ReadinessImpact != "READY" {
		t.Fatalf("unexpected freshness: %+v", summary)
	}
	if summary.FreshVehicleCount != 1 || summary.StaleVehicleCount != 0 || summary.CriticalVehicleCount != 0 {
		t.Fatalf("unexpected vehicle counts: %+v", summary)
	}
}

func TestMissionFreshnessStaleVehicle(t *testing.T) {
	engine := NewMissionStateEngine()
	now := time.Unix(1000, 0).UTC()
	if err := engine.Apply(buildFreshnessTestResult("ROVER-01", 10, now.Add(-45*time.Second), "power", "battery", "NOMINAL")); err != nil {
		t.Fatal(err)
	}
	summary, ok, err := engine.Freshness("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected freshness lookup: ok=%v err=%v", ok, err)
	}
	if summary.FreshnessStatus != "STALE" || summary.ReadinessImpact != "DEGRADED" {
		t.Fatalf("unexpected stale classification: %+v", summary)
	}
}

func TestMissionFreshnessVeryStaleVehicle(t *testing.T) {
	engine := NewMissionStateEngine()
	now := time.Unix(1000, 0).UTC()
	if err := engine.Apply(buildFreshnessTestResult("ROVER-01", 10, now.Add(-3*time.Minute), "power", "battery", "NOMINAL")); err != nil {
		t.Fatal(err)
	}
	summary, ok, err := engine.Freshness("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected freshness lookup: ok=%v err=%v", ok, err)
	}
	if summary.FreshnessStatus != "VERY_STALE" || summary.ReadinessImpact != "NOT_READY" {
		t.Fatalf("unexpected very-stale classification: %+v", summary)
	}
}

func TestMissionFreshnessTracksSubsystemsIndependently(t *testing.T) {
	engine := NewMissionStateEngine()
	now := time.Unix(1000, 0).UTC()
	fresh := buildFreshnessTestResult("ROVER-01", 20, now.Add(-5*time.Second), "mobility", "speed", "NOMINAL")
	stale := buildFreshnessTestResult("ROVER-01", 10, now.Add(-45*time.Second), "power", "battery", "NOMINAL")
	if err := engine.Apply(fresh); err != nil {
		t.Fatal(err)
	}
	if err := engine.Apply(stale); err != nil {
		t.Fatal(err)
	}
	summary, ok, err := engine.Freshness("TRISHULA", now, 30*time.Second, 2*time.Minute)
	if err != nil || !ok {
		t.Fatalf("unexpected freshness lookup: ok=%v err=%v", ok, err)
	}
	if summary.FreshVehicleCount != 1 || summary.StaleVehicleCount != 0 {
		t.Fatalf("vehicle should remain fresh based on latest vehicle update: %+v", summary)
	}
	if summary.FreshSubsystemCount != 1 || summary.StaleSubsystemCount != 1 {
		t.Fatalf("expected independent subsystem freshness: %+v", summary)
	}
	if summary.FreshnessStatus != "STALE" || summary.ReadinessImpact != "DEGRADED" {
		t.Fatalf("mission freshness should reflect stale subsystem: %+v", summary)
	}
}

func TestMissionFreshnessUnknownMission(t *testing.T) {
	engine := NewMissionStateEngine()
	_, ok, err := engine.Freshness("TRISHULA", time.Unix(1000, 0).UTC(), 30*time.Second, 2*time.Minute)
	if err != nil {
		t.Fatal(err)
	}
	if ok {
		t.Fatal("expected unknown mission")
	}
}

func TestMissionFreshnessRejectsInvalidThresholds(t *testing.T) {
	engine := NewMissionStateEngine()
	_, _, err := engine.Freshness("TRISHULA", time.Unix(1000, 0).UTC(), 2*time.Minute, 30*time.Second)
	if err == nil {
		t.Fatal("expected invalid threshold error")
	}
}
