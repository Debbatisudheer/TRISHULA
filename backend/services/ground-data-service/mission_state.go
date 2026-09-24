package main

import (
	"fmt"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

// MissionState is the ground-side operational projection for one mission.
// It aggregates the latest structured outputs without replacing PostgreSQL
// history or Redis current-state storage.
type MissionState struct {
	MissionID            string                         `json:"mission_id"`
	UpdatedAt            time.Time                      `json:"updated_at"`
	ActiveFaultCount     uint64                         `json:"active_fault_count"`
	HighestFaultSeverity string                         `json:"highest_fault_severity,omitempty"`
	Vehicles             map[string]VehicleMissionState `json:"vehicles"`
	highestFaultRank     int
}

type VehicleMissionState struct {
	SourceNode       string                            `json:"source_node"`
	LastSequence     uint64                            `json:"last_sequence"`
	LastKind         DataKind                          `json:"last_kind"`
	LastUpdatedAt    time.Time                         `json:"last_updated_at"`
	HealthStatus     string                            `json:"health_status"`
	Metrics          map[string]MetricState            `json:"metrics,omitempty"`
	TelemetryWindows map[string]TelemetryWindowSummary `json:"telemetry_windows,omitempty"`
	Subsystems       map[string]SubsystemState         `json:"subsystems,omitempty"`
	LastEvent        *MissionEventState                `json:"last_event,omitempty"`
	LastScience      *MissionScienceState              `json:"last_science,omitempty"`
	LastCommand      *MissionCommandState              `json:"last_command,omitempty"`
	LastFile         *MissionFileState                 `json:"last_file,omitempty"`
}

// VehicleStateSummary is the operator-oriented aggregation of one vehicle's
// mission state. It intentionally exposes a compact view without replacing
// the richer MissionState/VehicleMissionState projection.
// SubsystemState is the ground-side operational projection for one vehicle subsystem.
// It is derived from domain processing outputs and remains separate from durable history.
type SubsystemState struct {
	Name                 string                 `json:"name"`
	HealthStatus         string                 `json:"health_status"`
	LastSequence         uint64                 `json:"last_sequence"`
	LastKind             DataKind               `json:"last_kind"`
	LastUpdatedAt        time.Time              `json:"last_updated_at"`
	MetricCount          int                    `json:"metric_count"`
	Metrics              map[string]MetricState `json:"metrics,omitempty"`
	ActiveFaultCount     uint64                 `json:"active_fault_count"`
	HighestFaultSeverity string                 `json:"highest_fault_severity,omitempty"`
	LastOperation        string                 `json:"last_operation,omitempty"`
	LastEventType        string                 `json:"last_event_type,omitempty"`
	AggregationVersion   string                 `json:"aggregation_version"`
}

// SubsystemStateSummary is the compact query representation exposed to operators.
type SubsystemStateSummary = SubsystemState

type VehicleStateSummary struct {
	MissionID            string                            `json:"mission_id"`
	SourceNode           string                            `json:"source_node"`
	HealthStatus         string                            `json:"health_status"`
	LastSequence         uint64                            `json:"last_sequence"`
	LastKind             DataKind                          `json:"last_kind"`
	LastUpdatedAt        time.Time                         `json:"last_updated_at"`
	MetricCount          int                               `json:"metric_count"`
	SubsystemCount       int                               `json:"subsystem_count"`
	Subsystems           map[string]SubsystemStateSummary  `json:"subsystems,omitempty"`
	ActiveFaultCount     uint64                            `json:"active_fault_count"`
	HighestFaultSeverity string                            `json:"highest_fault_severity,omitempty"`
	HasScience           bool                              `json:"has_science"`
	HasCommand           bool                              `json:"has_command"`
	HasFile              bool                              `json:"has_file"`
	Metrics              map[string]MetricState            `json:"metrics,omitempty"`
	TelemetryWindows     map[string]TelemetryWindowSummary `json:"telemetry_windows,omitempty"`
	AggregationVersion   string                            `json:"aggregation_version"`
}

const currentVehicleAggregationVersion = "v0.9.72"
const currentSubsystemAggregationVersion = "v0.9.73"

type MetricState struct {
	Value            string `json:"value"`
	Unit             string `json:"unit,omitempty"`
	Quality          string `json:"quality,omitempty"`
	QualityClass     string `json:"quality_class,omitempty"`
	LimitStatus      string `json:"limit_status,omitempty"`
	OperationalState string `json:"operational_state,omitempty"`
	Sequence         uint64 `json:"sequence"`
	MissionTimestamp uint64 `json:"mission_timestamp_ns"`
}

type MissionEventState struct {
	EventType        string `json:"event_type"`
	Severity         string `json:"severity"`
	Lifecycle        string `json:"lifecycle"`
	IsFault          bool   `json:"is_fault"`
	FaultCode        string `json:"fault_code,omitempty"`
	ActiveFaultCount uint64 `json:"active_fault_count"`
	Sequence         uint64 `json:"sequence"`
}

type MissionScienceState struct {
	Instrument          string `json:"instrument"`
	Target              string `json:"target"`
	Measurement         string `json:"measurement"`
	QualityClass        string `json:"quality_class,omitempty"`
	ScientificUsability string `json:"scientific_usability,omitempty"`
	Sequence            uint64 `json:"sequence"`
}

type MissionCommandState struct {
	CommandID  string `json:"command_id"`
	Command    string `json:"command"`
	Target     string `json:"target"`
	Lifecycle  string `json:"lifecycle"`
	Transition string `json:"transition"`
	Terminal   bool   `json:"terminal"`
	Sequence   uint64 `json:"sequence"`
}

type MissionFileState struct {
	Filename  string `json:"filename"`
	MediaType string `json:"media_type,omitempty"`
	SizeBytes uint64 `json:"size_bytes,omitempty"`
	Sequence  uint64 `json:"sequence"`
}

// MissionStateEngine owns the in-process operational mission projection.
// It is derived state: durable records remain in PostgreSQL and the fast
// source-of-truth cache remains Redis.
type MissionStateEngine struct {
	mu               sync.RWMutex
	missions         map[string]MissionState
	telemetryWindows map[string]*telemetrySlidingWindow
}

func NewMissionStateEngine() *MissionStateEngine {
	return &MissionStateEngine{missions: make(map[string]MissionState), telemetryWindows: make(map[string]*telemetrySlidingWindow)}
}

func (e *MissionStateEngine) Apply(result ProcessingResult) error {
	if e == nil {
		return fmt.Errorf("mission state engine is nil")
	}
	missionID := strings.TrimSpace(result.MissionID)
	source := strings.TrimSpace(result.SourceNode)
	if missionID == "" || source == "" {
		return fmt.Errorf("mission_id and source_node are required for mission state")
	}

	e.mu.Lock()
	defer e.mu.Unlock()

	mission := e.missions[missionID]
	if mission.MissionID == "" {
		mission = MissionState{
			MissionID: missionID,
			Vehicles:  make(map[string]VehicleMissionState),
		}
	}
	vehicle := mission.Vehicles[source]
	if vehicle.SourceNode == "" {
		vehicle = VehicleMissionState{
			SourceNode:   source,
			HealthStatus: "UNKNOWN",
			Metrics:      make(map[string]MetricState),
			Subsystems:   make(map[string]SubsystemState),
		}
	}

	// Do not let older records roll the vehicle-level operational projection backward.
	if result.SequenceNumber >= vehicle.LastSequence {
		vehicle.LastSequence = result.SequenceNumber
		vehicle.LastKind = result.Kind
		vehicle.LastUpdatedAt = result.ProcessedAt
		switch result.Kind {
		case KindTelemetry:
			e.applyTelemetry(&vehicle, result)
			e.applyTelemetryWindow(&vehicle, result)
		case KindEvent:
			e.applyEvent(&mission, &vehicle, result)
		case KindScience:
			e.applyScience(&vehicle, result)
		case KindCommand:
			e.applyCommand(&vehicle, result)
		case KindFile:
			e.applyFile(&vehicle, result)
		}
		if result.ProcessedAt.After(mission.UpdatedAt) {
			mission.UpdatedAt = result.ProcessedAt
		}
	}

	// Subsystem state is independently ordered. A record older than the
	// vehicle-wide latest sequence can still be newer for its subsystem.
	e.applySubsystem(&vehicle, result)

	e.recomputeVehicleHealth(&vehicle)
	mission.Vehicles[source] = vehicle
	e.missions[missionID] = mission
	return nil
}

func (e *MissionStateEngine) applyTelemetry(vehicle *VehicleMissionState, result ProcessingResult) {
	metric := strings.TrimSpace(result.Attributes["metric"])
	if metric == "" {
		return
	}
	current, exists := vehicle.Metrics[metric]
	if exists && result.SequenceNumber < current.Sequence {
		return
	}
	vehicle.Metrics[metric] = MetricState{
		Value:            result.Attributes["value"],
		Unit:             result.Attributes["unit"],
		Quality:          result.Attributes["quality"],
		QualityClass:     result.Attributes["quality_class"],
		LimitStatus:      result.Attributes["limit_status"],
		OperationalState: result.Attributes["operational_state"],
		Sequence:         result.SequenceNumber,
		MissionTimestamp: result.MissionTimestampNS,
	}
}

func (e *MissionStateEngine) applyTelemetryWindow(vehicle *VehicleMissionState, result ProcessingResult) {
	metric := strings.TrimSpace(result.Attributes["metric"])
	if metric == "" {
		return
	}
	value, err := parseTelemetryWindowValue(strings.TrimSpace(result.Attributes["value"]))
	if err != nil {
		return
	}
	key := result.MissionID + "\x00" + vehicle.SourceNode + "\x00" + metric
	window := e.telemetryWindows[key]
	if window == nil {
		window = newTelemetrySlidingWindow(defaultTelemetryWindowSize)
		e.telemetryWindows[key] = window
	}
	if window.append(result.SequenceNumber, value) {
		if vehicle.TelemetryWindows == nil {
			vehicle.TelemetryWindows = make(map[string]TelemetryWindowSummary)
		}
		vehicle.TelemetryWindows[metric] = window.summary(metric)
	}
}

func (e *MissionStateEngine) applyEvent(mission *MissionState, vehicle *VehicleMissionState, result ProcessingResult) {
	activeFaultCount := parseUintOrZero(result.Attributes["active_fault_count"])
	mission.ActiveFaultCount = activeFaultCount
	severity := strings.ToUpper(strings.TrimSpace(result.Attributes["severity"]))
	if activeFaultCount == 0 {
		mission.HighestFaultSeverity = ""
		mission.highestFaultRank = 0
	} else if rank := eventSeverityRank(severity); rank > mission.highestFaultRank {
		mission.HighestFaultSeverity = severity
		mission.highestFaultRank = rank
	}
	vehicle.LastEvent = &MissionEventState{
		EventType:        result.Attributes["event_type"],
		Severity:         severity,
		Lifecycle:        result.Attributes["lifecycle"],
		IsFault:          strings.EqualFold(result.Attributes["is_fault"], "true"),
		FaultCode:        result.Attributes["fault_code"],
		ActiveFaultCount: activeFaultCount,
		Sequence:         result.SequenceNumber,
	}
}

func (e *MissionStateEngine) applyScience(vehicle *VehicleMissionState, result ProcessingResult) {
	vehicle.LastScience = &MissionScienceState{
		Instrument:          result.Attributes["instrument"],
		Target:              result.Attributes["target"],
		Measurement:         result.Attributes["measurement"],
		QualityClass:        result.Attributes["quality_class"],
		ScientificUsability: result.Attributes["scientific_usability"],
		Sequence:            result.SequenceNumber,
	}
}

func (e *MissionStateEngine) applyCommand(vehicle *VehicleMissionState, result ProcessingResult) {
	vehicle.LastCommand = &MissionCommandState{
		CommandID:  result.Attributes["command_id"],
		Command:    result.Attributes["command"],
		Target:     result.Attributes["target"],
		Lifecycle:  result.Attributes["lifecycle"],
		Transition: result.Attributes["transition"],
		Terminal:   strings.EqualFold(result.Attributes["terminal"], "true"),
		Sequence:   result.SequenceNumber,
	}
}

func (e *MissionStateEngine) applyFile(vehicle *VehicleMissionState, result ProcessingResult) {
	vehicle.LastFile = &MissionFileState{
		Filename:  result.Attributes["filename"],
		MediaType: result.Attributes["media_type"],
		SizeBytes: parseUintOrZero(result.Attributes["size_bytes"]),
		Sequence:  result.SequenceNumber,
	}
}

func subsystemForResult(result ProcessingResult) string {
	if explicit := strings.ToLower(strings.TrimSpace(result.Attributes["subsystem"])); explicit != "" {
		return explicit
	}

	switch result.Kind {
	case KindTelemetry:
		metric := strings.ToLower(strings.TrimSpace(result.Attributes["metric"]))
		switch metric {
		case "battery", "voltage", "current", "power":
			return "power"
		case "temperature", "thermal":
			return "thermal"
		case "speed", "wheel_speed", "wheel":
			return "mobility"
		case "signal_strength", "packet_loss", "latency", "link_quality":
			return "communications"
		default:
			return "telemetry"
		}
	case KindScience:
		return "science"
	case KindEvent:
		return "fault-management"
	case KindCommand:
		return "command"
	case KindFile:
		return "data"
	default:
		return "general"
	}
}

func (e *MissionStateEngine) applySubsystem(vehicle *VehicleMissionState, result ProcessingResult) {
	name := subsystemForResult(result)
	if name == "" {
		return
	}
	if vehicle.Subsystems == nil {
		vehicle.Subsystems = make(map[string]SubsystemState)
	}
	subsystem := vehicle.Subsystems[name]
	if subsystem.Name == "" {
		subsystem = SubsystemState{
			Name:               name,
			HealthStatus:       "UNKNOWN",
			Metrics:            make(map[string]MetricState),
			AggregationVersion: currentSubsystemAggregationVersion,
		}
	}
	if result.SequenceNumber < subsystem.LastSequence {
		return
	}
	subsystem.LastSequence = result.SequenceNumber
	subsystem.LastKind = result.Kind
	subsystem.LastUpdatedAt = result.ProcessedAt
	subsystem.LastOperation = result.Operation

	switch result.Kind {
	case KindTelemetry:
		metric := strings.TrimSpace(result.Attributes["metric"])
		if metric != "" {
			current, exists := subsystem.Metrics[metric]
			if !exists || result.SequenceNumber >= current.Sequence {
				subsystem.Metrics[metric] = MetricState{
					Value:            result.Attributes["value"],
					Unit:             result.Attributes["unit"],
					Quality:          result.Attributes["quality"],
					QualityClass:     result.Attributes["quality_class"],
					LimitStatus:      result.Attributes["limit_status"],
					OperationalState: result.Attributes["operational_state"],
					Sequence:         result.SequenceNumber,
					MissionTimestamp: result.MissionTimestampNS,
				}
			}
		}
	case KindEvent:
		subsystem.ActiveFaultCount = parseUintOrZero(result.Attributes["active_fault_count"])
		subsystem.HighestFaultSeverity = strings.ToUpper(strings.TrimSpace(result.Attributes["severity"]))
		subsystem.LastEventType = result.Attributes["event_type"]
	}

	subsystem.MetricCount = len(subsystem.Metrics)
	e.recomputeSubsystemHealth(&subsystem)
	vehicle.Subsystems[name] = subsystem
}

func (e *MissionStateEngine) recomputeSubsystemHealth(subsystem *SubsystemState) {
	health := "NOMINAL"
	for _, metric := range subsystem.Metrics {
		switch strings.ToUpper(strings.TrimSpace(metric.OperationalState)) {
		case "CRITICAL":
			health = "CRITICAL"
		case "LOW_POWER", "HOT", "COLD":
			if health != "CRITICAL" {
				health = "DEGRADED"
			}
		}
		if strings.EqualFold(metric.QualityClass, "BAD") && health != "CRITICAL" {
			health = "DEGRADED"
		}
	}
	if subsystem.ActiveFaultCount > 0 {
		switch strings.ToUpper(subsystem.HighestFaultSeverity) {
		case string(EventCritical):
			health = "CRITICAL"
		case string(EventError), string(EventWarning):
			if health != "CRITICAL" {
				health = "DEGRADED"
			}
		}
	}
	subsystem.HealthStatus = health
}

func (e *MissionStateEngine) recomputeVehicleHealth(vehicle *VehicleMissionState) {
	health := "NOMINAL"
	for _, metric := range vehicle.Metrics {
		switch strings.ToUpper(metric.OperationalState) {
		case "CRITICAL":
			health = "CRITICAL"
		case "LOW_POWER", "HOT", "COLD":
			if health != "CRITICAL" {
				health = "DEGRADED"
			}
		}
		if strings.EqualFold(metric.QualityClass, "BAD") && health != "CRITICAL" {
			health = "DEGRADED"
		}
	}
	if vehicle.LastEvent != nil {
		severity := strings.ToUpper(vehicle.LastEvent.Severity)
		if vehicle.LastEvent.IsFault && vehicle.LastEvent.Lifecycle != string(EventCleared) {
			switch severity {
			case string(EventCritical):
				health = "CRITICAL"
			case string(EventError), string(EventWarning):
				if health != "CRITICAL" {
					health = "DEGRADED"
				}
			}
		}
	}
	vehicle.HealthStatus = health
}

// ReplaceMission atomically replaces one mission projection with a cloned
// state. A nil/empty state removes the mission. It is used by durable-history
// synchronization after a complete replay succeeds.
func (e *MissionStateEngine) ReplaceMission(missionID string, mission MissionState) {
	if e == nil {
		return
	}
	missionID = strings.TrimSpace(missionID)
	if missionID == "" {
		return
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if mission.MissionID == "" {
		delete(e.missions, missionID)
		return
	}
	e.missions[missionID] = cloneMissionState(mission)
	for key := range e.telemetryWindows {
		if strings.HasPrefix(key, missionID+"\x00") {
			delete(e.telemetryWindows, key)
		}
	}
}

func (e *MissionStateEngine) Snapshot() map[string]MissionState {
	e.mu.RLock()
	defer e.mu.RUnlock()
	out := make(map[string]MissionState, len(e.missions))
	for missionID, mission := range e.missions {
		out[missionID] = cloneMissionState(mission)
	}
	return out
}

func (e *MissionStateEngine) Get(missionID string) (MissionState, bool) {
	e.mu.RLock()
	defer e.mu.RUnlock()
	mission, ok := e.missions[strings.TrimSpace(missionID)]
	if !ok {
		return MissionState{}, false
	}
	return cloneMissionState(mission), true
}

func (e *MissionStateEngine) VehicleSummaries(missionID string) []VehicleStateSummary {
	e.mu.RLock()
	defer e.mu.RUnlock()
	mission, ok := e.missions[strings.TrimSpace(missionID)]
	if !ok {
		return []VehicleStateSummary{}
	}

	out := make([]VehicleStateSummary, 0, len(mission.Vehicles))
	for _, vehicle := range mission.Vehicles {
		out = append(out, buildVehicleStateSummary(missionID, vehicle))
	}
	sort.Slice(out, func(i, j int) bool { return out[i].SourceNode < out[j].SourceNode })
	return out
}

func (e *MissionStateEngine) Vehicle(missionID, sourceNode string) (VehicleStateSummary, bool) {
	e.mu.RLock()
	defer e.mu.RUnlock()
	mission, ok := e.missions[strings.TrimSpace(missionID)]
	if !ok {
		return VehicleStateSummary{}, false
	}
	vehicle, ok := mission.Vehicles[strings.TrimSpace(sourceNode)]
	if !ok {
		return VehicleStateSummary{}, false
	}
	return buildVehicleStateSummary(missionID, vehicle), true
}

func (e *MissionStateEngine) SubsystemSummaries(missionID, sourceNode string) []SubsystemStateSummary {
	e.mu.RLock()
	defer e.mu.RUnlock()
	mission, ok := e.missions[strings.TrimSpace(missionID)]
	if !ok {
		return []SubsystemStateSummary{}
	}
	vehicle, ok := mission.Vehicles[strings.TrimSpace(sourceNode)]
	if !ok {
		return []SubsystemStateSummary{}
	}

	out := make([]SubsystemStateSummary, 0, len(vehicle.Subsystems))
	for _, subsystem := range vehicle.Subsystems {
		copied := subsystem
		copied.Metrics = copyMetricStateMap(subsystem.Metrics)
		out = append(out, copied)
	}
	sort.Slice(out, func(i, j int) bool {
		return out[i].Name < out[j].Name
	})
	return out
}

func (e *MissionStateEngine) Subsystem(missionID, sourceNode, subsystem string) (SubsystemStateSummary, bool) {
	e.mu.RLock()
	defer e.mu.RUnlock()
	mission, ok := e.missions[strings.TrimSpace(missionID)]
	if !ok {
		return SubsystemStateSummary{}, false
	}
	vehicle, ok := mission.Vehicles[strings.TrimSpace(sourceNode)]
	if !ok {
		return SubsystemStateSummary{}, false
	}
	state, ok := vehicle.Subsystems[strings.ToLower(strings.TrimSpace(subsystem))]
	if !ok {
		return SubsystemStateSummary{}, false
	}
	state.Metrics = copyMetricStateMap(state.Metrics)
	return state, true
}

func buildVehicleStateSummary(missionID string, vehicle VehicleMissionState) VehicleStateSummary {
	summary := VehicleStateSummary{
		MissionID:          missionID,
		SourceNode:         vehicle.SourceNode,
		HealthStatus:       vehicle.HealthStatus,
		LastSequence:       vehicle.LastSequence,
		LastKind:           vehicle.LastKind,
		LastUpdatedAt:      vehicle.LastUpdatedAt,
		MetricCount:        len(vehicle.Metrics),
		SubsystemCount:     len(vehicle.Subsystems),
		Subsystems:         make(map[string]SubsystemStateSummary, len(vehicle.Subsystems)),
		HasScience:         vehicle.LastScience != nil,
		HasCommand:         vehicle.LastCommand != nil,
		HasFile:            vehicle.LastFile != nil,
		Metrics:            make(map[string]MetricState, len(vehicle.Metrics)),
		TelemetryWindows:   make(map[string]TelemetryWindowSummary, len(vehicle.TelemetryWindows)),
		AggregationVersion: currentVehicleAggregationVersion,
	}
	if vehicle.LastEvent != nil {
		summary.ActiveFaultCount = vehicle.LastEvent.ActiveFaultCount
		if vehicle.LastEvent.IsFault && vehicle.LastEvent.Lifecycle != string(EventCleared) {
			summary.HighestFaultSeverity = vehicle.LastEvent.Severity
		}
	}
	for metric, state := range vehicle.Metrics {
		summary.Metrics[metric] = state
	}
	for metric, window := range vehicle.TelemetryWindows {
		summary.TelemetryWindows[metric] = window
	}
	for name, state := range vehicle.Subsystems {
		state.Metrics = copyMetricStateMap(state.Metrics)
		summary.Subsystems[name] = state
	}
	return summary
}

func eventSeverityRank(raw string) int {
	switch strings.ToUpper(strings.TrimSpace(raw)) {
	case string(EventCritical):
		return 4
	case string(EventError):
		return 3
	case string(EventWarning):
		return 2
	case string(EventInfo):
		return 1
	default:
		return 0
	}
}

func cloneMissionState(mission MissionState) MissionState {
	cloned := mission
	cloned.Vehicles = make(map[string]VehicleMissionState, len(mission.Vehicles))
	for source, vehicle := range mission.Vehicles {
		copied := vehicle
		copied.Metrics = make(map[string]MetricState, len(vehicle.Metrics))
		copied.TelemetryWindows = make(map[string]TelemetryWindowSummary, len(vehicle.TelemetryWindows))
		for metric, window := range vehicle.TelemetryWindows {
			copied.TelemetryWindows[metric] = window
		}
		for metric, state := range vehicle.Metrics {
			copied.Metrics[metric] = state
		}
		copied.Subsystems = make(map[string]SubsystemState, len(vehicle.Subsystems))
		for name, subsystem := range vehicle.Subsystems {
			clonedSubsystem := subsystem
			clonedSubsystem.Metrics = copyMetricStateMap(subsystem.Metrics)
			copied.Subsystems[name] = clonedSubsystem
		}
		if vehicle.LastEvent != nil {
			event := *vehicle.LastEvent
			copied.LastEvent = &event
		}
		if vehicle.LastScience != nil {
			science := *vehicle.LastScience
			copied.LastScience = &science
		}
		if vehicle.LastCommand != nil {
			command := *vehicle.LastCommand
			copied.LastCommand = &command
		}
		if vehicle.LastFile != nil {
			file := *vehicle.LastFile
			copied.LastFile = &file
		}
		cloned.Vehicles[source] = copied
	}
	return cloned
}

func copyMetricStateMap(input map[string]MetricState) map[string]MetricState {
	if input == nil {
		return map[string]MetricState{}
	}
	out := make(map[string]MetricState, len(input))
	for key, value := range input {
		out[key] = value
	}
	return out
}

func parseUintOrZero(raw string) uint64 {
	value, err := strconv.ParseUint(strings.TrimSpace(raw), 10, 64)
	if err != nil {
		return 0
	}
	return value
}
