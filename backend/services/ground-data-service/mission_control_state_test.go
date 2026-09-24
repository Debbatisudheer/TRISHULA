package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestBuildMissionControlStateSnapshot(t *testing.T) {
	processed := time.Date(2026, 9, 18, 8, 0, 0, 0, time.UTC)
	states := map[string]MissionState{
		"TRISHULA": {
			MissionID:            "TRISHULA",
			UpdatedAt:            processed,
			ActiveFaultCount:     1,
			HighestFaultSeverity: "warning",
			Vehicles: map[string]VehicleMissionState{
				"ROVER-01": {
					SourceNode:    "ROVER-01",
					LastSequence:  42,
					LastKind:      KindTelemetry,
					LastUpdatedAt: processed,
					HealthStatus:  "NOMINAL",
					Metrics:       map[string]MetricState{"battery": {Value: "87", Unit: "%", Sequence: 42}},
					Subsystems:    map[string]SubsystemState{"power": {Name: "power", HealthStatus: "NOMINAL", AggregationVersion: currentSubsystemAggregationVersion}},
				},
			},
		},
	}
	snapshot := buildMissionControlStateSnapshot(states, func(string) (MissionHealthSummary, bool) {
		return MissionHealthSummary{HealthStatus: "NOMINAL", Readiness: "READY"}, true
	}, func(string) (MissionFreshnessSummary, bool) {
		return MissionFreshnessSummary{FreshnessStatus: "FRESH"}, true
	}, processed, "mc-state-test-001")

	if snapshot.Version != currentMissionControlStateVersion || snapshot.StateModelVersion != currentMissionControlStateVersion {
		t.Fatalf("versions = %q/%q", snapshot.Version, snapshot.StateModelVersion)
	}
	if snapshot.MissionCount != 1 || len(snapshot.Missions) != 1 {
		t.Fatalf("mission count = %d/%d", snapshot.MissionCount, len(snapshot.Missions))
	}
	mission := snapshot.Missions[0]
	if mission.MissionID != "TRISHULA" || mission.VehicleCount != 1 || len(mission.Vehicles) != 1 {
		t.Fatalf("mission = %+v", mission)
	}
	if mission.HealthStatus != "NOMINAL" || mission.Readiness != "READY" || mission.FreshnessStatus != "FRESH" {
		t.Fatalf("mission statuses = %+v", mission)
	}
	vehicle := mission.Vehicles[0]
	if vehicle.SourceNode != "ROVER-01" || vehicle.LastSequence != 42 || vehicle.MetricCount != 1 || vehicle.SubsystemCount != 1 {
		t.Fatalf("vehicle = %+v", vehicle)
	}
}

func TestBuildMissionControlStateSnapshotEmpty(t *testing.T) {
	snapshot := buildMissionControlStateSnapshot(nil, nil, nil, time.Time{}, "mc-state-test-002")
	if snapshot.Missions == nil {
		t.Fatal("missions should be a non-nil empty slice")
	}
	if snapshot.MissionCount != 0 || snapshot.Status != "ready" {
		t.Fatalf("snapshot = %+v", snapshot)
	}
}

func TestHandleMissionControlState(t *testing.T) {
	router := NewRecordRouter()
	r := httptest.NewRequest(http.MethodGet, "/v1/mission-control/state", nil)
	r.Header.Set("X-Request-ID", "mc-state-test-003")
	w := httptest.NewRecorder()
	handleMissionControlState(w, r, router)
	if w.Code != http.StatusOK {
		t.Fatalf("status = %d", w.Code)
	}
	if got := w.Header().Get("X-TRISHULA-API-VERSION"); got != currentMissionControlStateVersion {
		t.Fatalf("api version = %q", got)
	}
	if !strings.Contains(w.Body.String(), `"request_id":"mc-state-test-003"`) {
		t.Fatalf("request id missing: %s", w.Body.String())
	}
}

func TestHandleMissionControlStateRejectsNonGet(t *testing.T) {
	r := httptest.NewRequest(http.MethodPost, "/v1/mission-control/state", nil)
	w := httptest.NewRecorder()
	handleMissionControlState(w, r, nil)
	if w.Code != http.StatusMethodNotAllowed {
		t.Fatalf("status = %d", w.Code)
	}
}
