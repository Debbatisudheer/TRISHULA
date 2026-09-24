package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestBuildMissionControlSnapshot(t *testing.T) {
	generated := time.Date(2026, 9, 18, 8, 0, 0, 0, time.UTC)
	missions := []MissionSummary{{MissionID: "TRISHULA", VehicleCount: 1, HealthStatus: "NOMINAL", Readiness: "READY", FreshnessStatus: "FRESH"}}
	snapshot := buildMissionControlSnapshot(missions, true, generated, "mc-test-001")
	if snapshot.Version != currentMissionControlVersion || snapshot.MissionControlVersion != currentMissionControlVersion {
		t.Fatalf("version = %q/%q", snapshot.Version, snapshot.MissionControlVersion)
	}
	if snapshot.Status != "ready" || snapshot.MissionCount != 1 || len(snapshot.Missions) != 1 {
		t.Fatalf("snapshot = %+v", snapshot)
	}
	if !snapshot.Capabilities.MissionState || !snapshot.Capabilities.CommandOperations {
		t.Fatalf("state capabilities not enabled: %+v", snapshot.Capabilities)
	}
}

func TestBuildMissionControlSnapshotDegraded(t *testing.T) {
	snapshot := buildMissionControlSnapshot(nil, false, time.Time{}, "mc-test-002")
	if snapshot.Status != "degraded" {
		t.Fatalf("status = %q", snapshot.Status)
	}
	if snapshot.Capabilities.MissionState || snapshot.Capabilities.VehicleState || snapshot.Capabilities.Health {
		t.Fatalf("state capabilities should be disabled: %+v", snapshot.Capabilities)
	}
	if snapshot.Missions == nil {
		t.Fatal("missions should be a non-nil empty slice")
	}
}

func TestHandleMissionControlSnapshot(t *testing.T) {
	r := httptest.NewRequest(http.MethodGet, "/v1/mission-control", nil)
	r.Header.Set("X-Request-ID", "mc-test-003")
	w := httptest.NewRecorder()
	handleMissionControlSnapshot(w, r, nil, true)
	if w.Code != http.StatusOK {
		t.Fatalf("status = %d", w.Code)
	}
	if got := w.Header().Get("X-TRISHULA-API-VERSION"); got != currentMissionControlVersion {
		t.Fatalf("api version header = %q", got)
	}
	if !strings.Contains(w.Body.String(), `"request_id":"mc-test-003"`) {
		t.Fatalf("request id missing: %s", w.Body.String())
	}
}

func TestHandleMissionControlSnapshotRejectsNonGet(t *testing.T) {
	r := httptest.NewRequest(http.MethodPost, "/v1/mission-control", nil)
	w := httptest.NewRecorder()
	handleMissionControlSnapshot(w, r, nil, true)
	if w.Code != http.StatusMethodNotAllowed {
		t.Fatalf("status = %d", w.Code)
	}
}
