package main

import (
	"fmt"
	"sort"
	"strings"
	"time"
)

const (
	currentMissionFreshnessVersion  = "v0.9.75"
	defaultMissionFreshnessWarn     = 30 * time.Second
	defaultMissionFreshnessCritical = 2 * time.Minute
)

// MissionFreshnessSummary is a read-only projection of how recently each
// vehicle/subsystem reported usable ground-side state. It does not replace
// durable history, Redis current state, mission health, or sequence ordering.
type MissionFreshnessSummary struct {
	MissionID              string                    `json:"mission_id"`
	FreshnessStatus        string                    `json:"freshness_status"`
	ReadinessImpact        string                    `json:"readiness_impact"`
	AsOf                   time.Time                 `json:"as_of"`
	MissionUpdatedAt       time.Time                 `json:"mission_updated_at"`
	AgeSeconds             int64                     `json:"age_seconds"`
	WarningAfterSeconds    int64                     `json:"warning_after_seconds"`
	CriticalAfterSeconds   int64                     `json:"critical_after_seconds"`
	VehicleCount           int                       `json:"vehicle_count"`
	FreshVehicleCount      int                       `json:"fresh_vehicle_count"`
	StaleVehicleCount      int                       `json:"stale_vehicle_count"`
	CriticalVehicleCount   int                       `json:"critical_vehicle_count"`
	UnknownVehicleCount    int                       `json:"unknown_vehicle_count"`
	SubsystemCount         int                       `json:"subsystem_count"`
	FreshSubsystemCount    int                       `json:"fresh_subsystem_count"`
	StaleSubsystemCount    int                       `json:"stale_subsystem_count"`
	CriticalSubsystemCount int                       `json:"critical_subsystem_count"`
	UnknownSubsystemCount  int                       `json:"unknown_subsystem_count"`
	FreshnessVersion       string                    `json:"freshness_version"`
	Vehicles               []MissionFreshnessVehicle `json:"vehicles,omitempty"`
}

// MissionFreshnessVehicle is the freshness view for one vehicle.
type MissionFreshnessVehicle struct {
	SourceNode         string    `json:"source_node"`
	FreshnessStatus    string    `json:"freshness_status"`
	LastSequence       uint64    `json:"last_sequence"`
	LastUpdatedAt      time.Time `json:"last_updated_at"`
	AgeSeconds         int64     `json:"age_seconds"`
	SubsystemCount     int       `json:"subsystem_count"`
	FreshSubsystems    int       `json:"fresh_subsystems"`
	StaleSubsystems    int       `json:"stale_subsystems"`
	CriticalSubsystems int       `json:"critical_subsystems"`
	UnknownSubsystems  int       `json:"unknown_subsystems"`
}

func classifyFreshness(updatedAt, now time.Time, warnAfter, criticalAfter time.Duration) (string, int64) {
	if updatedAt.IsZero() {
		return "UNKNOWN", 0
	}
	age := now.Sub(updatedAt)
	if age < 0 {
		age = 0
	}
	ageSeconds := int64(age / time.Second)
	switch {
	case age > criticalAfter:
		return "VERY_STALE", ageSeconds
	case age > warnAfter:
		return "STALE", ageSeconds
	default:
		return "FRESH", ageSeconds
	}
}

func validateFreshnessThresholds(warnAfter, criticalAfter time.Duration) error {
	if warnAfter <= 0 {
		return fmt.Errorf("warning threshold must be positive")
	}
	if criticalAfter <= warnAfter {
		return fmt.Errorf("critical threshold must be greater than warning threshold")
	}
	return nil
}

func (e *MissionStateEngine) Freshness(missionID string, now time.Time, warnAfter, criticalAfter time.Duration) (MissionFreshnessSummary, bool, error) {
	if e == nil {
		return MissionFreshnessSummary{}, false, fmt.Errorf("mission state engine is nil")
	}
	if err := validateFreshnessThresholds(warnAfter, criticalAfter); err != nil {
		return MissionFreshnessSummary{}, false, err
	}
	if now.IsZero() {
		now = time.Now().UTC()
	} else {
		now = now.UTC()
	}

	e.mu.RLock()
	mission, ok := e.missions[strings.TrimSpace(missionID)]
	e.mu.RUnlock()
	if !ok {
		return MissionFreshnessSummary{}, false, nil
	}

	summary := MissionFreshnessSummary{
		MissionID:            mission.MissionID,
		FreshnessStatus:      "UNKNOWN",
		ReadinessImpact:      "UNKNOWN",
		AsOf:                 now,
		MissionUpdatedAt:     mission.UpdatedAt,
		WarningAfterSeconds:  int64(warnAfter / time.Second),
		CriticalAfterSeconds: int64(criticalAfter / time.Second),
		VehicleCount:         len(mission.Vehicles),
		FreshnessVersion:     currentMissionFreshnessVersion,
	}

	vehicles := make([]MissionFreshnessVehicle, 0, len(mission.Vehicles))
	for _, vehicle := range mission.Vehicles {
		status, ageSeconds := classifyFreshness(vehicle.LastUpdatedAt, now, warnAfter, criticalAfter)
		hv := MissionFreshnessVehicle{
			SourceNode:      vehicle.SourceNode,
			FreshnessStatus: status,
			LastSequence:    vehicle.LastSequence,
			LastUpdatedAt:   vehicle.LastUpdatedAt,
			AgeSeconds:      ageSeconds,
			SubsystemCount:  len(vehicle.Subsystems),
		}
		switch status {
		case "FRESH":
			summary.FreshVehicleCount++
		case "STALE":
			summary.StaleVehicleCount++
		case "VERY_STALE":
			summary.CriticalVehicleCount++
		default:
			summary.UnknownVehicleCount++
		}

		for _, subsystem := range vehicle.Subsystems {
			summary.SubsystemCount++
			subsystemStatus, _ := classifyFreshness(subsystem.LastUpdatedAt, now, warnAfter, criticalAfter)
			switch subsystemStatus {
			case "FRESH":
				summary.FreshSubsystemCount++
				hv.FreshSubsystems++
			case "STALE":
				summary.StaleSubsystemCount++
				hv.StaleSubsystems++
			case "VERY_STALE":
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

	if !mission.UpdatedAt.IsZero() {
		_, summary.AgeSeconds = classifyFreshness(mission.UpdatedAt, now, warnAfter, criticalAfter)
	} else {
		summary.AgeSeconds = 0
	}

	switch {
	case summary.VehicleCount == 0:
		summary.FreshnessStatus = "UNKNOWN"
		summary.ReadinessImpact = "UNKNOWN"
	case summary.CriticalVehicleCount > 0 || summary.CriticalSubsystemCount > 0:
		summary.FreshnessStatus = "VERY_STALE"
		summary.ReadinessImpact = "NOT_READY"
	case summary.StaleVehicleCount > 0 || summary.StaleSubsystemCount > 0:
		summary.FreshnessStatus = "STALE"
		summary.ReadinessImpact = "DEGRADED"
	case summary.FreshVehicleCount == summary.VehicleCount && summary.FreshSubsystemCount == summary.SubsystemCount:
		summary.FreshnessStatus = "FRESH"
		summary.ReadinessImpact = "READY"
	default:
		summary.FreshnessStatus = "UNKNOWN"
		summary.ReadinessImpact = "UNKNOWN"
	}
	return summary, true, nil
}
