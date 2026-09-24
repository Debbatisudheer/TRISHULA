package main

import (
	"net/http"
	"sort"
	"strings"
	"time"
)

const currentMissionControlDashboardVersion = "v0.9.104"

type MissionControlDashboardSnapshot struct {
	APIEnvelope
	Service          string                           `json:"service"`
	DashboardVersion string                           `json:"dashboard_version"`
	GeneratedAt      time.Time                        `json:"generated_at"`
	Status           string                           `json:"status"`
	MissionCount     int                              `json:"mission_count"`
	Summary          MissionControlDashboardSummary   `json:"summary"`
	Missions         []MissionControlDashboardMission `json:"missions"`
}

type MissionControlDashboardSummary struct {
	VehicleCount      int    `json:"vehicle_count"`
	ActiveAlertCount  int    `json:"active_alert_count"`
	TotalAlertCount   int    `json:"total_alert_count"`
	CommandCount      int    `json:"command_count"`
	CompletedCommands int    `json:"completed_commands"`
	FailedCommands    int    `json:"failed_commands"`
	TelemetryAccepted uint64 `json:"telemetry_accepted"`
	EventsAccepted    uint64 `json:"events_accepted"`
}

type MissionControlDashboardMission struct {
	MissionID         string                           `json:"mission_id"`
	UpdatedAt         time.Time                        `json:"updated_at"`
	HealthStatus      string                           `json:"health_status"`
	Readiness         string                           `json:"readiness"`
	FreshnessStatus   string                           `json:"freshness_status"`
	VehicleCount      int                              `json:"vehicle_count"`
	ActiveAlertCount  int                              `json:"active_alert_count"`
	TotalAlertCount   int                              `json:"total_alert_count"`
	CommandCount      int                              `json:"command_count"`
	CompletedCommands int                              `json:"completed_commands"`
	FailedCommands    int                              `json:"failed_commands"`
	Vehicles          []MissionControlDashboardVehicle `json:"vehicles"`
}

type MissionControlDashboardVehicle struct {
	SourceNode           string    `json:"source_node"`
	HealthStatus         string    `json:"health_status"`
	LastSequence         uint64    `json:"last_sequence"`
	LastKind             DataKind  `json:"last_kind"`
	LastUpdatedAt        time.Time `json:"last_updated_at"`
	ActiveFaultCount     uint64    `json:"active_fault_count"`
	HighestFaultSeverity string    `json:"highest_fault_severity,omitempty"`
	MetricCount          int       `json:"metric_count"`
	SubsystemCount       int       `json:"subsystem_count"`
	HasScience           bool      `json:"has_science"`
	HasCommand           bool      `json:"has_command"`
	HasFile              bool      `json:"has_file"`
}

func buildMissionControlDashboard(consumer *KafkaConsumer, generatedAt time.Time, requestIDValue string) MissionControlDashboardSnapshot {
	if generatedAt.IsZero() {
		generatedAt = time.Now().UTC()
	} else {
		generatedAt = generatedAt.UTC()
	}
	out := MissionControlDashboardSnapshot{APIEnvelope: APIEnvelope{RequestID: requestIDValue, Version: currentMissionControlDashboardVersion}, Service: "trishula-ground-data-service", DashboardVersion: currentMissionControlDashboardVersion, GeneratedAt: generatedAt, Status: "ready", Missions: []MissionControlDashboardMission{}}
	if consumer == nil {
		out.Status = "degraded"
		return out
	}
	states := consumer.MissionState()
	missionIDs := make([]string, 0, len(states))
	for id := range states {
		missionIDs = append(missionIDs, id)
	}
	sort.Strings(missionIDs)

	for _, missionID := range missionIDs {
		state := states[missionID]
		health, ok := consumer.MissionHealth(missionID)
		healthStatus, readiness := "UNKNOWN", "UNKNOWN"
		if ok {
			healthStatus, readiness = health.HealthStatus, health.Readiness
		}
		freshnessStatus := "UNKNOWN"
		if freshness, found, err := consumer.MissionFreshness(missionID, generatedAt, defaultMissionFreshnessWarn, defaultMissionFreshnessCritical); err == nil && found {
			freshnessStatus = freshness.FreshnessStatus
		}
		alerts := consumer.Alerts(missionID)
		executions := consumer.CommandExecutionForMission(missionID)
		completed, failed := 0, 0
		for _, execution := range executions {
			switch execution.State {
			case CommandCompletedExec:
				completed++
			case CommandFailedExec, CommandRejectedExec, CommandTimeoutExec, CommandCancelledExec:
				failed++
			}
		}
		vehicles := consumer.VehicleSummaries(missionID)
		dashboardVehicles := make([]MissionControlDashboardVehicle, 0, len(vehicles))
		for _, v := range vehicles {
			dashboardVehicles = append(dashboardVehicles, MissionControlDashboardVehicle{SourceNode: v.SourceNode, HealthStatus: v.HealthStatus, LastSequence: v.LastSequence, LastKind: v.LastKind, LastUpdatedAt: v.LastUpdatedAt, ActiveFaultCount: v.ActiveFaultCount, HighestFaultSeverity: v.HighestFaultSeverity, MetricCount: v.MetricCount, SubsystemCount: v.SubsystemCount, HasScience: v.HasScience, HasCommand: v.HasCommand, HasFile: v.HasFile})
		}
		sort.Slice(dashboardVehicles, func(i, j int) bool { return dashboardVehicles[i].SourceNode < dashboardVehicles[j].SourceNode })
		out.Missions = append(out.Missions, MissionControlDashboardMission{MissionID: missionID, UpdatedAt: state.UpdatedAt, HealthStatus: healthStatus, Readiness: readiness, FreshnessStatus: freshnessStatus, VehicleCount: len(vehicles), ActiveAlertCount: alerts.ActiveCount, TotalAlertCount: alerts.TotalCount, CommandCount: len(executions), CompletedCommands: completed, FailedCommands: failed, Vehicles: dashboardVehicles})
	}
	for _, m := range out.Missions {
		out.Summary.VehicleCount += m.VehicleCount
		out.Summary.ActiveAlertCount += m.ActiveAlertCount
		out.Summary.TotalAlertCount += m.TotalAlertCount
		out.Summary.CommandCount += m.CommandCount
		out.Summary.CompletedCommands += m.CompletedCommands
		out.Summary.FailedCommands += m.FailedCommands
	}
	routes := consumer.router.Snapshot()
	out.Summary.TelemetryAccepted = routes.Telemetry.Accepted
	out.Summary.EventsAccepted = routes.Event.Accepted
	out.MissionCount = len(out.Missions)
	return out
}

func handleMissionControlDashboard(w http.ResponseWriter, r *http.Request, consumer *KafkaConsumer) {
	if strings.ToUpper(r.Method) != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		return
	}
	w.Header().Set("X-TRISHULA-API-VERSION", currentMissionControlDashboardVersion)
	writeJSON(w, http.StatusOK, buildMissionControlDashboard(consumer, time.Now().UTC(), requestID(r)))
}
