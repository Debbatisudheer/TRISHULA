package main

import (
	"sort"
	"strings"
	"time"
)

// MissionHealthSummary is a compact operator-facing health/readiness projection
// derived from the existing mission and subsystem state. It does not replace
// durable history or the richer MissionState projection.
type MissionHealthSummary struct {
	MissionID              string                 `json:"mission_id"`
	HealthStatus           string                 `json:"health_status"`
	Readiness              string                 `json:"readiness"`
	UpdatedAt              time.Time              `json:"updated_at"`
	VehicleCount           int                    `json:"vehicle_count"`
	NominalVehicleCount    int                    `json:"nominal_vehicle_count"`
	DegradedVehicleCount   int                    `json:"degraded_vehicle_count"`
	CriticalVehicleCount   int                    `json:"critical_vehicle_count"`
	UnknownVehicleCount    int                    `json:"unknown_vehicle_count"`
	SubsystemCount         int                    `json:"subsystem_count"`
	NominalSubsystemCount  int                    `json:"nominal_subsystem_count"`
	DegradedSubsystemCount int                    `json:"degraded_subsystem_count"`
	CriticalSubsystemCount int                    `json:"critical_subsystem_count"`
	UnknownSubsystemCount  int                    `json:"unknown_subsystem_count"`
	ActiveFaultCount       uint64                 `json:"active_fault_count"`
	HighestFaultSeverity   string                 `json:"highest_fault_severity,omitempty"`
	HealthVersion          string                 `json:"health_version"`
	Vehicles               []MissionHealthVehicle `json:"vehicles,omitempty"`
}

// MissionHealthVehicle gives the health contribution of one vehicle to the
// mission summary while preserving the existing VehicleStateSummary boundary.
type MissionHealthVehicle struct {
	SourceNode           string `json:"source_node"`
	HealthStatus         string `json:"health_status"`
	LastSequence         uint64 `json:"last_sequence"`
	ActiveFaultCount     uint64 `json:"active_fault_count"`
	HighestFaultSeverity string `json:"highest_fault_severity,omitempty"`
	SubsystemCount       int    `json:"subsystem_count"`
	CriticalSubsystems   int    `json:"critical_subsystems"`
	DegradedSubsystems   int    `json:"degraded_subsystems"`
	NominalSubsystems    int    `json:"nominal_subsystems"`
	UnknownSubsystems    int    `json:"unknown_subsystems"`
}

const currentMissionHealthVersion = "v0.9.74"

func buildMissionHealthSummary(mission MissionState) MissionHealthSummary {
	summary := MissionHealthSummary{
		MissionID:            mission.MissionID,
		UpdatedAt:            mission.UpdatedAt,
		HealthStatus:         "UNKNOWN",
		Readiness:            "UNKNOWN",
		VehicleCount:         len(mission.Vehicles),
		ActiveFaultCount:     mission.ActiveFaultCount,
		HighestFaultSeverity: strings.ToUpper(strings.TrimSpace(mission.HighestFaultSeverity)),
		HealthVersion:        currentMissionHealthVersion,
	}

	vehicles := make([]MissionHealthVehicle, 0, len(mission.Vehicles))
	for _, vehicle := range mission.Vehicles {
		hv := MissionHealthVehicle{
			SourceNode:       vehicle.SourceNode,
			HealthStatus:     strings.ToUpper(strings.TrimSpace(vehicle.HealthStatus)),
			LastSequence:     vehicle.LastSequence,
			SubsystemCount:   len(vehicle.Subsystems),
			ActiveFaultCount: 0,
		}
		if vehicle.LastEvent != nil {
			hv.ActiveFaultCount = vehicle.LastEvent.ActiveFaultCount
			if vehicle.LastEvent.IsFault && vehicle.LastEvent.Lifecycle != string(EventCleared) {
				hv.HighestFaultSeverity = strings.ToUpper(strings.TrimSpace(vehicle.LastEvent.Severity))
			}
		}
		if hv.ActiveFaultCount == 0 {
			hv.ActiveFaultCount = 0
		}
		summary.ActiveFaultCount += 0 // mission already carries the authoritative count.

		switch hv.HealthStatus {
		case "NOMINAL":
			summary.NominalVehicleCount++
		case "DEGRADED":
			summary.DegradedVehicleCount++
		case "CRITICAL":
			summary.CriticalVehicleCount++
		default:
			summary.UnknownVehicleCount++
		}

		for _, subsystem := range vehicle.Subsystems {
			summary.SubsystemCount++
			health := strings.ToUpper(strings.TrimSpace(subsystem.HealthStatus))
			switch health {
			case "NOMINAL":
				summary.NominalSubsystemCount++
				hv.NominalSubsystems++
			case "DEGRADED":
				summary.DegradedSubsystemCount++
				hv.DegradedSubsystems++
			case "CRITICAL":
				summary.CriticalSubsystemCount++
				hv.CriticalSubsystems++
			default:
				summary.UnknownSubsystemCount++
				hv.UnknownSubsystems++
			}
		}
		vehicles = append(vehicles, hv)
	}
	sort.Slice(vehicles, func(i, j int) bool { return vehicles[i].SourceNode < vehicles[j].SourceNode })
	summary.Vehicles = vehicles

	switch {
	case summary.VehicleCount == 0:
		summary.HealthStatus = "UNKNOWN"
		summary.Readiness = "UNKNOWN"
	case summary.CriticalVehicleCount > 0 || summary.CriticalSubsystemCount > 0 || highestSeverityAtLeast(summary.HighestFaultSeverity, string(EventCritical)):
		summary.HealthStatus = "CRITICAL"
		summary.Readiness = "NOT_READY"
	case summary.DegradedVehicleCount > 0 || summary.DegradedSubsystemCount > 0 || summary.ActiveFaultCount > 0:
		summary.HealthStatus = "DEGRADED"
		summary.Readiness = "DEGRADED"
	default:
		summary.HealthStatus = "NOMINAL"
		summary.Readiness = "READY"
	}

	return summary
}

func highestSeverityAtLeast(actual, threshold string) bool {
	return eventSeverityRank(actual) >= eventSeverityRank(threshold) && eventSeverityRank(actual) > 0
}

func (e *MissionStateEngine) Health(missionID string) (MissionHealthSummary, bool) {
	e.mu.RLock()
	defer e.mu.RUnlock()
	mission, ok := e.missions[strings.TrimSpace(missionID)]
	if !ok {
		return MissionHealthSummary{}, false
	}
	return buildMissionHealthSummary(mission), true
}
