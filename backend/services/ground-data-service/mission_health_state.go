package main

import (
	"sort"
	"strings"
	"time"
)

const (
	currentHealthStateVersion  = "v0.9.76"
	defaultHealthWarnAfter     = 30 * time.Second
	defaultHealthCriticalAfter = 2 * time.Minute
)

// MissionHealthState is the combined operator-facing vehicle/mission health
// projection. It deliberately composes existing freshness, telemetry-quality,
// subsystem, and fault projections instead of replacing them.
type MissionHealthState struct {
	MissionID              string               `json:"mission_id"`
	HealthStatus           string               `json:"health_status"`
	Readiness              string               `json:"readiness"`
	AsOf                   time.Time            `json:"as_of"`
	WarningAfterSeconds    int64                `json:"warning_after_seconds"`
	CriticalAfterSeconds   int64                `json:"critical_after_seconds"`
	VehicleCount           int                  `json:"vehicle_count"`
	NominalVehicleCount    int                  `json:"nominal_vehicle_count"`
	DegradedVehicleCount   int                  `json:"degraded_vehicle_count"`
	CriticalVehicleCount   int                  `json:"critical_vehicle_count"`
	UnknownVehicleCount    int                  `json:"unknown_vehicle_count"`
	FreshVehicleCount      int                  `json:"fresh_vehicle_count"`
	StaleVehicleCount      int                  `json:"stale_vehicle_count"`
	VeryStaleVehicleCount  int                  `json:"very_stale_vehicle_count"`
	UnknownFreshnessCount  int                  `json:"unknown_freshness_count"`
	GoodMetricCount        int                  `json:"good_metric_count"`
	DegradedMetricCount    int                  `json:"degraded_metric_count"`
	BadMetricCount         int                  `json:"bad_metric_count"`
	UnknownMetricCount     int                  `json:"unknown_metric_count"`
	ActiveFaultCount       uint64               `json:"active_fault_count"`
	HighestFaultSeverity   string               `json:"highest_fault_severity,omitempty"`
	NominalSubsystemCount  int                  `json:"nominal_subsystem_count"`
	DegradedSubsystemCount int                  `json:"degraded_subsystem_count"`
	CriticalSubsystemCount int                  `json:"critical_subsystem_count"`
	UnknownSubsystemCount  int                  `json:"unknown_subsystem_count"`
	HealthStateVersion     string               `json:"health_state_version"`
	Vehicles               []VehicleHealthState `json:"vehicles,omitempty"`
}

// VehicleHealthState is the combined health view for one vehicle/source node.
type VehicleHealthState struct {
	SourceNode             string    `json:"source_node"`
	HealthStatus           string    `json:"health_status"`
	Readiness              string    `json:"readiness"`
	LastSequence           uint64    `json:"last_sequence"`
	LastUpdatedAt          time.Time `json:"last_updated_at"`
	AgeSeconds             int64     `json:"age_seconds"`
	FreshnessStatus        string    `json:"freshness_status"`
	QualityStatus          string    `json:"quality_status"`
	FaultStatus            string    `json:"fault_status"`
	SubsystemStatus        string    `json:"subsystem_status"`
	ActiveFaultCount       uint64    `json:"active_fault_count"`
	HighestFaultSeverity   string    `json:"highest_fault_severity,omitempty"`
	GoodMetricCount        int       `json:"good_metric_count"`
	DegradedMetricCount    int       `json:"degraded_metric_count"`
	BadMetricCount         int       `json:"bad_metric_count"`
	UnknownMetricCount     int       `json:"unknown_metric_count"`
	NominalSubsystemCount  int       `json:"nominal_subsystem_count"`
	DegradedSubsystemCount int       `json:"degraded_subsystem_count"`
	CriticalSubsystemCount int       `json:"critical_subsystem_count"`
	UnknownSubsystemCount  int       `json:"unknown_subsystem_count"`
}

func (e *MissionStateEngine) HealthState(missionID string, now time.Time, warnAfter, criticalAfter time.Duration) (MissionHealthState, bool, error) {
	if e == nil {
		return MissionHealthState{}, false, nil
	}
	if err := validateFreshnessThresholds(warnAfter, criticalAfter); err != nil {
		return MissionHealthState{}, false, err
	}
	if now.IsZero() {
		now = time.Now().UTC()
	} else {
		now = now.UTC()
	}

	e.mu.RLock()
	mission, ok := e.missions[strings.TrimSpace(missionID)]
	if !ok {
		e.mu.RUnlock()
		return MissionHealthState{}, false, nil
	}
	mission = cloneMissionState(mission)
	e.mu.RUnlock()

	state := MissionHealthState{
		MissionID:            mission.MissionID,
		AsOf:                 now,
		WarningAfterSeconds:  int64(warnAfter / time.Second),
		CriticalAfterSeconds: int64(criticalAfter / time.Second),
		VehicleCount:         len(mission.Vehicles),
		ActiveFaultCount:     mission.ActiveFaultCount,
		HighestFaultSeverity: strings.ToUpper(strings.TrimSpace(mission.HighestFaultSeverity)),
		HealthStateVersion:   currentHealthStateVersion,
	}
	if state.HighestFaultSeverity == "" && mission.ActiveFaultCount == 0 {
		state.FaultsNone()
	}

	vehicles := make([]VehicleHealthState, 0, len(mission.Vehicles))
	for _, vehicle := range mission.Vehicles {
		vh := aggregateVehicleHealth(vehicle, now, warnAfter, criticalAfter)
		vehicles = append(vehicles, vh)

		switch vh.HealthStatus {
		case "NOMINAL":
			state.NominalVehicleCount++
		case "DEGRADED":
			state.DegradedVehicleCount++
		case "CRITICAL":
			state.CriticalVehicleCount++
		default:
			state.UnknownVehicleCount++
		}
		switch vh.FreshnessStatus {
		case "FRESH":
			state.FreshVehicleCount++
		case "STALE":
			state.StaleVehicleCount++
		case "VERY_STALE":
			state.VeryStaleVehicleCount++
		default:
			state.UnknownFreshnessCount++
		}
		state.GoodMetricCount += vh.GoodMetricCount
		state.DegradedMetricCount += vh.DegradedMetricCount
		state.BadMetricCount += vh.BadMetricCount
		state.UnknownMetricCount += vh.UnknownMetricCount
		state.NominalSubsystemCount += vh.NominalSubsystemCount
		state.DegradedSubsystemCount += vh.DegradedSubsystemCount
		state.CriticalSubsystemCount += vh.CriticalSubsystemCount
		state.UnknownSubsystemCount += vh.UnknownSubsystemCount
	}
	sort.Slice(vehicles, func(i, j int) bool { return vehicles[i].SourceNode < vehicles[j].SourceNode })
	state.Vehicles = vehicles

	state.HealthStatus, state.Readiness = aggregateMissionHealth(state)
	return state, true, nil
}

func aggregateVehicleHealth(vehicle VehicleMissionState, now time.Time, warnAfter, criticalAfter time.Duration) VehicleHealthState {
	freshnessStatus, ageSeconds := classifyFreshness(vehicle.LastUpdatedAt, now, warnAfter, criticalAfter)
	qualityStatus, good, degraded, bad, unknown := aggregateVehicleQuality(vehicle)
	subsystemStatus, nominalSubs, degradedSubs, criticalSubs, unknownSubs := aggregateSubsystems(vehicle)
	faultStatus := aggregateFaultStatus(vehicle)

	status := "UNKNOWN"
	switch {
	case freshnessStatus == "VERY_STALE", qualityStatus == "BAD", subsystemStatus == "CRITICAL", faultStatus == "CRITICAL":
		status = "CRITICAL"
	case freshnessStatus == "STALE", qualityStatus == "DEGRADED", subsystemStatus == "DEGRADED", faultStatus == "ERROR" || faultStatus == "WARNING":
		status = "DEGRADED"
	case freshnessStatus == "FRESH" && qualityStatus == "GOOD" && subsystemStatus == "NOMINAL" && faultStatus == "NONE":
		status = "NOMINAL"
	default:
		status = "UNKNOWN"
	}

	readiness := "UNKNOWN"
	switch status {
	case "NOMINAL":
		readiness = "READY"
	case "DEGRADED":
		readiness = "DEGRADED"
	case "CRITICAL":
		readiness = "NOT_READY"
	}

	return VehicleHealthState{
		SourceNode:             vehicle.SourceNode,
		HealthStatus:           status,
		Readiness:              readiness,
		LastSequence:           vehicle.LastSequence,
		LastUpdatedAt:          vehicle.LastUpdatedAt,
		AgeSeconds:             ageSeconds,
		FreshnessStatus:        freshnessStatus,
		QualityStatus:          qualityStatus,
		FaultStatus:            faultStatus,
		SubsystemStatus:        subsystemStatus,
		ActiveFaultCount:       vehicleFaultCount(vehicle),
		HighestFaultSeverity:   vehicleHighestFaultSeverity(vehicle),
		GoodMetricCount:        good,
		DegradedMetricCount:    degraded,
		BadMetricCount:         bad,
		UnknownMetricCount:     unknown,
		NominalSubsystemCount:  nominalSubs,
		DegradedSubsystemCount: degradedSubs,
		CriticalSubsystemCount: criticalSubs,
		UnknownSubsystemCount:  unknownSubs,
	}
}

func aggregateMissionHealth(state MissionHealthState) (string, string) {
	if state.VehicleCount == 0 {
		return "UNKNOWN", "UNKNOWN"
	}
	switch {
	case state.CriticalVehicleCount > 0 || state.CriticalSubsystemCount > 0 || state.VeryStaleVehicleCount > 0 || eventSeverityRank(state.HighestFaultSeverity) >= eventSeverityRank(string(EventCritical)):
		return "CRITICAL", "NOT_READY"
	case state.DegradedVehicleCount > 0 || state.DegradedSubsystemCount > 0 || state.StaleVehicleCount > 0 || state.DegradedMetricCount > 0 || state.BadMetricCount > 0 || state.ActiveFaultCount > 0:
		return "DEGRADED", "DEGRADED"
	case state.NominalVehicleCount == state.VehicleCount && state.FreshVehicleCount == state.VehicleCount:
		return "NOMINAL", "READY"
	default:
		return "UNKNOWN", "UNKNOWN"
	}
}

func aggregateVehicleQuality(vehicle VehicleMissionState) (status string, good, degraded, bad, unknown int) {
	if len(vehicle.Metrics) == 0 {
		return "UNKNOWN", 0, 0, 0, 0
	}
	for _, metric := range vehicle.Metrics {
		switch strings.ToUpper(strings.TrimSpace(metric.QualityClass)) {
		case "GOOD":
			good++
		case "DEGRADED":
			degraded++
		case "BAD":
			bad++
		default:
			unknown++
		}
	}
	switch {
	case bad > 0:
		return "BAD", good, degraded, bad, unknown
	case degraded > 0 || unknown > 0:
		return "DEGRADED", good, degraded, bad, unknown
	case good > 0:
		return "GOOD", good, degraded, bad, unknown
	default:
		return "UNKNOWN", good, degraded, bad, unknown
	}
}

func aggregateSubsystems(vehicle VehicleMissionState) (status string, nominal, degraded, critical, unknown int) {
	if len(vehicle.Subsystems) == 0 {
		return "UNKNOWN", 0, 0, 0, 0
	}
	for _, subsystem := range vehicle.Subsystems {
		switch strings.ToUpper(strings.TrimSpace(subsystem.HealthStatus)) {
		case "NOMINAL":
			nominal++
		case "DEGRADED":
			degraded++
		case "CRITICAL":
			critical++
		default:
			unknown++
		}
	}
	switch {
	case critical > 0:
		return "CRITICAL", nominal, degraded, critical, unknown
	case degraded > 0:
		return "DEGRADED", nominal, degraded, critical, unknown
	case nominal > 0 && unknown == 0:
		return "NOMINAL", nominal, degraded, critical, unknown
	default:
		return "UNKNOWN", nominal, degraded, critical, unknown
	}
}

func aggregateFaultStatus(vehicle VehicleMissionState) string {
	if vehicle.LastEvent == nil {
		return "NONE"
	}
	if !vehicle.LastEvent.IsFault || vehicle.LastEvent.Lifecycle == string(EventCleared) || vehicle.LastEvent.ActiveFaultCount == 0 {
		return "NONE"
	}
	switch eventSeverityRank(vehicle.LastEvent.Severity) {
	case 4:
		return "CRITICAL"
	case 3:
		return "ERROR"
	case 2:
		return "WARNING"
	default:
		return "NONE"
	}
}

func vehicleFaultCount(vehicle VehicleMissionState) uint64 {
	if vehicle.LastEvent == nil {
		return 0
	}
	if vehicle.LastEvent.Lifecycle == string(EventCleared) || !vehicle.LastEvent.IsFault {
		return 0
	}
	return vehicle.LastEvent.ActiveFaultCount
}

func vehicleHighestFaultSeverity(vehicle VehicleMissionState) string {
	if vehicle.LastEvent == nil || vehicle.LastEvent.Lifecycle == string(EventCleared) || !vehicle.LastEvent.IsFault {
		return ""
	}
	return strings.ToUpper(strings.TrimSpace(vehicle.LastEvent.Severity))
}

func (s *MissionHealthState) FaultsNone() {
	if s != nil && s.HighestFaultSeverity == "" {
		s.HighestFaultSeverity = ""
	}
}
