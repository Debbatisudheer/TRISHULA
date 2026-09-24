package main

import (
	"net/http"
	"sort"
	"strings"
	"time"
)

const currentMissionControlStateVersion = "v0.9.97"

type MissionControlStateSnapshot struct {
	APIEnvelope
	Service           string                       `json:"service"`
	StateModelVersion string                       `json:"state_model_version"`
	GeneratedAt       time.Time                    `json:"generated_at"`
	Status            string                       `json:"status"`
	MissionCount      int                          `json:"mission_count"`
	Missions          []MissionControlMissionState `json:"missions"`
}

type MissionControlMissionState struct {
	MissionID            string                `json:"mission_id"`
	UpdatedAt            time.Time             `json:"updated_at"`
	HealthStatus         string                `json:"health_status"`
	Readiness            string                `json:"readiness"`
	FreshnessStatus      string                `json:"freshness_status"`
	ActiveFaultCount     uint64                `json:"active_fault_count"`
	HighestFaultSeverity string                `json:"highest_fault_severity,omitempty"`
	VehicleCount         int                   `json:"vehicle_count"`
	Vehicles             []VehicleStateSummary `json:"vehicles"`
}

func buildMissionControlStateSnapshot(states map[string]MissionState, health func(string) (MissionHealthSummary, bool), freshness func(string) (MissionFreshnessSummary, bool), generatedAt time.Time, requestIDValue string) MissionControlStateSnapshot {
	if generatedAt.IsZero() {
		generatedAt = time.Now().UTC()
	} else {
		generatedAt = generatedAt.UTC()
	}

	missions := make([]MissionControlMissionState, 0, len(states))
	for missionID, state := range states {
		vehicles := make([]VehicleStateSummary, 0, len(state.Vehicles))
		for sourceNode := range state.Vehicles {
			if summary, ok := vehicleStateSummaryFromMissionState(state, sourceNode); ok {
				vehicles = append(vehicles, summary)
			}
		}
		sort.Slice(vehicles, func(i, j int) bool { return vehicles[i].SourceNode < vehicles[j].SourceNode })

		mission := MissionControlMissionState{
			MissionID:            missionID,
			UpdatedAt:            state.UpdatedAt,
			ActiveFaultCount:     state.ActiveFaultCount,
			HighestFaultSeverity: strings.ToUpper(strings.TrimSpace(state.HighestFaultSeverity)),
			VehicleCount:         len(vehicles),
			Vehicles:             vehicles,
		}
		if mission.Vehicles == nil {
			mission.Vehicles = make([]VehicleStateSummary, 0)
		}
		if health != nil {
			if value, found := health(missionID); found {
				mission.HealthStatus = value.HealthStatus
				mission.Readiness = value.Readiness
			}
		}
		if mission.HealthStatus == "" {
			mission.HealthStatus = "UNKNOWN"
		}
		if mission.Readiness == "" {
			mission.Readiness = "UNKNOWN"
		}
		if freshness != nil {
			if value, found := freshness(missionID); found {
				mission.FreshnessStatus = value.FreshnessStatus
			}
		}
		if mission.FreshnessStatus == "" {
			mission.FreshnessStatus = "UNKNOWN"
		}
		missions = append(missions, mission)
	}
	sort.Slice(missions, func(i, j int) bool { return missions[i].MissionID < missions[j].MissionID })

	status := "ready"
	if len(missions) == 0 {
		status = "ready"
	}
	return MissionControlStateSnapshot{
		APIEnvelope:       APIEnvelope{RequestID: requestIDValue, Version: currentMissionControlStateVersion},
		Service:           "trishula-ground-data-service",
		StateModelVersion: currentMissionControlStateVersion,
		GeneratedAt:       generatedAt,
		Status:            status,
		MissionCount:      len(missions),
		Missions:          missions,
	}
}

func vehicleStateSummaryFromMissionState(mission MissionState, sourceNode string) (VehicleStateSummary, bool) {
	vehicle, found := mission.Vehicles[sourceNode]
	if !found {
		return VehicleStateSummary{}, false
	}
	subs := make(map[string]SubsystemStateSummary, len(vehicle.Subsystems))
	for name, subsystem := range vehicle.Subsystems {
		subs[name] = subsystem
	}
	metrics := make(map[string]MetricState, len(vehicle.Metrics))
	for name, metric := range vehicle.Metrics {
		metrics[name] = metric
	}
	return VehicleStateSummary{
		MissionID:            mission.MissionID,
		SourceNode:           vehicle.SourceNode,
		HealthStatus:         vehicle.HealthStatus,
		LastSequence:         vehicle.LastSequence,
		LastKind:             vehicle.LastKind,
		LastUpdatedAt:        vehicle.LastUpdatedAt,
		MetricCount:          len(metrics),
		SubsystemCount:       len(subs),
		Subsystems:           subs,
		ActiveFaultCount:     missionControlVehicleFaultCount(vehicle),
		HighestFaultSeverity: highestVehicleFaultSeverity(vehicle),
		HasScience:           vehicle.LastScience != nil,
		HasCommand:           vehicle.LastCommand != nil,
		HasFile:              vehicle.LastFile != nil,
		Metrics:              metrics,
		AggregationVersion:   currentVehicleAggregationVersion,
	}, true
}

func missionControlVehicleFaultCount(vehicle VehicleMissionState) uint64 {
	if vehicle.LastEvent == nil {
		return 0
	}
	return vehicle.LastEvent.ActiveFaultCount
}

func highestVehicleFaultSeverity(vehicle VehicleMissionState) string {
	if vehicle.LastEvent == nil {
		return ""
	}
	return strings.ToUpper(strings.TrimSpace(vehicle.LastEvent.Severity))
}

func handleMissionControlState(w http.ResponseWriter, r *http.Request, router *RecordRouter) {
	if strings.ToUpper(r.Method) != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		return
	}
	states := map[string]MissionState{}
	var health func(string) (MissionHealthSummary, bool)
	var freshness func(string) (MissionFreshnessSummary, bool)
	if router != nil {
		states = router.MissionState()
		health = router.MissionHealth
		now := time.Now().UTC()
		freshness = func(missionID string) (MissionFreshnessSummary, bool) {
			value, found, err := router.MissionFreshness(missionID, now, defaultMissionFreshnessWarn, defaultMissionFreshnessCritical)
			if err != nil {
				return MissionFreshnessSummary{}, false
			}
			return value, found
		}
	}
	w.Header().Set("X-TRISHULA-API-VERSION", currentMissionControlStateVersion)
	writeJSON(w, http.StatusOK, buildMissionControlStateSnapshot(states, health, freshness, time.Now().UTC(), requestID(r)))
}
