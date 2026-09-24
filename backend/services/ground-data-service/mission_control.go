package main

import (
	"net/http"
	"strings"
	"time"
)

const currentMissionControlVersion = "v0.9.96"

type MissionControlSnapshot struct {
	APIEnvelope
	Service               string                     `json:"service"`
	MissionControlVersion string                     `json:"mission_control_version"`
	GeneratedAt           time.Time                  `json:"generated_at"`
	Status                string                     `json:"status"`
	MissionCount          int                        `json:"mission_count"`
	Missions              []MissionSummary           `json:"missions"`
	Capabilities          MissionControlCapabilities `json:"capabilities"`
}

type MissionControlCapabilities struct {
	MissionOverview   bool `json:"mission_overview"`
	MissionState      bool `json:"mission_state"`
	VehicleState      bool `json:"vehicle_state"`
	Health            bool `json:"health"`
	Freshness         bool `json:"freshness"`
	CommandOperations bool `json:"command_operations"`
	TelemetryViews    bool `json:"telemetry_views"`
	EventViews        bool `json:"event_views"`
}

func buildMissionControlSnapshot(missions []MissionSummary, stateAvailable bool, generatedAt time.Time, requestIDValue string) MissionControlSnapshot {
	if missions == nil {
		missions = make([]MissionSummary, 0)
	}
	if generatedAt.IsZero() {
		generatedAt = time.Now().UTC()
	} else {
		generatedAt = generatedAt.UTC()
	}
	status := "ready"
	if !stateAvailable {
		status = "degraded"
	}
	return MissionControlSnapshot{
		APIEnvelope:           APIEnvelope{RequestID: requestIDValue, Version: currentMissionControlVersion},
		Service:               "trishula-ground-data-service",
		MissionControlVersion: currentMissionControlVersion,
		GeneratedAt:           generatedAt,
		Status:                status,
		MissionCount:          len(missions),
		Missions:              missions,
		Capabilities: MissionControlCapabilities{
			MissionOverview:   true,
			MissionState:      stateAvailable,
			VehicleState:      stateAvailable,
			Health:            stateAvailable,
			Freshness:         stateAvailable,
			CommandOperations: stateAvailable,
			TelemetryViews:    true,
			EventViews:        true,
		},
	}
}

func handleMissionControlSnapshot(w http.ResponseWriter, r *http.Request, service *MissionService, stateAvailable bool) {
	if method := strings.ToUpper(r.Method); method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		return
	}
	missions := make([]MissionSummary, 0)
	if service != nil {
		if listed := service.List(r.Context(), time.Now().UTC()); listed != nil {
			missions = listed
		}
	}
	w.Header().Set("X-TRISHULA-API-VERSION", currentMissionControlVersion)
	writeJSON(w, http.StatusOK, buildMissionControlSnapshot(missions, stateAvailable, time.Now().UTC(), requestID(r)))
}
