package main

import (
	"context"
	"fmt"
	"sort"
	"strconv"
	"strings"
	"time"
)

const currentMissionOperationsVersion = "v0.9.81"

// MissionOperationsView is the operator-facing read model for mission operations.
// It combines the already-verified mission state/health/freshness/consistency
// projections with durable command history and a recent mission timeline.
// It does not execute commands; command execution remains behind the existing
// command/vehicle boundaries.
type MissionOperationsView struct {
	MissionID                string                     `json:"mission_id"`
	GeneratedAt              time.Time                  `json:"generated_at"`
	OperationalPosture       string                     `json:"operational_posture"`
	MissionOperationsVersion string                     `json:"mission_operations_version"`
	Health                   MissionHealthSummary       `json:"health"`
	Freshness                MissionFreshnessSummary    `json:"freshness"`
	Consistency              *MissionStateConsistency   `json:"consistency,omitempty"`
	Vehicles                 []MissionOperationsVehicle `json:"vehicles"`
	Commands                 MissionCommandOperations   `json:"commands"`
	Timeline                 []MissionTimelineEntry     `json:"timeline"`
}

type MissionOperationsVehicle struct {
	SourceNode           string               `json:"source_node"`
	HealthStatus         string               `json:"health_status"`
	FreshnessStatus      string               `json:"freshness_status"`
	LastSequence         uint64               `json:"last_sequence"`
	LastUpdatedAt        time.Time            `json:"last_updated_at"`
	ActiveFaultCount     uint64               `json:"active_fault_count"`
	HighestFaultSeverity string               `json:"highest_fault_severity,omitempty"`
	LastCommand          *MissionCommandState `json:"last_command,omitempty"`
}

type MissionCommandOperations struct {
	UniqueCommands    int                              `json:"unique_commands"`
	ActiveCommands    int                              `json:"active_commands"`
	CompletedCommands int                              `json:"completed_commands"`
	FailedCommands    int                              `json:"failed_commands"`
	RejectedCommands  int                              `json:"rejected_commands"`
	Latest            []MissionCommandState            `json:"latest,omitempty"`
	Execution         []CommandExecutionReconciliation `json:"execution,omitempty"`
}

type MissionTimelineEntry struct {
	RecordID           string   `json:"record_id"`
	Kind               DataKind `json:"kind"`
	SourceNode         string   `json:"source_node"`
	MissionTimestampNS uint64   `json:"mission_timestamp_ns"`
	SequenceNumber     uint64   `json:"sequence_number"`
	Summary            string   `json:"summary"`
	Status             string   `json:"status"`
	Severity           string   `json:"severity,omitempty"`
	Lifecycle          string   `json:"lifecycle,omitempty"`
}

type MissionOperationsService struct {
	missionService *MissionService
	repo           RecordRepository
}

func NewMissionOperationsService(router *RecordRouter, repo RecordRepository) *MissionOperationsService {
	return &MissionOperationsService{
		missionService: NewMissionService(router, repo),
		repo:           repo,
	}
}

func (s *MissionOperationsService) Operations(ctx context.Context, missionID string, now time.Time, timelineLimit int) (MissionOperationsView, bool, error) {
	missionID = strings.TrimSpace(missionID)
	if missionID == "" {
		return MissionOperationsView{}, false, fmt.Errorf("missionID is required")
	}
	if s == nil || s.missionService == nil || s.repo == nil {
		return MissionOperationsView{}, false, fmt.Errorf("mission operations service is not configured")
	}
	if timelineLimit <= 0 {
		timelineLimit = 50
	}
	if timelineLimit > 200 {
		timelineLimit = 200
	}

	detail, found, err := s.missionService.Get(ctx, missionID, now)
	if err != nil || !found {
		return MissionOperationsView{}, found, err
	}

	timeline, err := s.Timeline(ctx, missionID, timelineLimit)
	if err != nil {
		return MissionOperationsView{}, false, err
	}
	commands, err := s.commandOperations(ctx, missionID)
	if err != nil {
		return MissionOperationsView{}, false, err
	}
	if s.missionService.router != nil {
		commands.Execution = s.missionService.router.CommandExecutionForMission(missionID)
	}

	vehicles := make([]MissionOperationsVehicle, 0, len(detail.State.Vehicles))
	freshnessBySource := make(map[string]MissionFreshnessVehicle, len(detail.Freshness.Vehicles))
	for _, vehicle := range detail.Freshness.Vehicles {
		freshnessBySource[vehicle.SourceNode] = vehicle
	}
	for _, vehicle := range detail.Health.Vehicles {
		item := MissionOperationsVehicle{
			SourceNode:           vehicle.SourceNode,
			HealthStatus:         vehicle.HealthStatus,
			LastSequence:         vehicle.LastSequence,
			ActiveFaultCount:     vehicle.ActiveFaultCount,
			HighestFaultSeverity: vehicle.HighestFaultSeverity,
		}
		if f, ok := freshnessBySource[vehicle.SourceNode]; ok {
			item.FreshnessStatus = f.FreshnessStatus
			item.LastUpdatedAt = f.LastUpdatedAt
		}
		if state, ok := detail.State.Vehicles[vehicle.SourceNode]; ok {
			item.LastCommand = state.LastCommand
			if item.LastUpdatedAt.IsZero() {
				item.LastUpdatedAt = state.LastUpdatedAt
			}
		}
		vehicles = append(vehicles, item)
	}
	sort.Slice(vehicles, func(i, j int) bool { return vehicles[i].SourceNode < vehicles[j].SourceNode })

	posture := deriveOperationalPosture(detail.Health, detail.Freshness, detail.Consistency)
	generatedAt := now
	if generatedAt.IsZero() {
		generatedAt = time.Now().UTC()
	} else {
		generatedAt = generatedAt.UTC()
	}

	return MissionOperationsView{
		MissionID:                missionID,
		GeneratedAt:              generatedAt,
		OperationalPosture:       posture,
		MissionOperationsVersion: currentMissionOperationsVersion,
		Health:                   detail.Health,
		Freshness:                detail.Freshness,
		Consistency:              detail.Consistency,
		Vehicles:                 vehicles,
		Commands:                 commands,
		Timeline:                 timeline,
	}, true, nil
}

func deriveOperationalPosture(health MissionHealthSummary, freshness MissionFreshnessSummary, consistency *MissionStateConsistency) string {
	if strings.EqualFold(health.HealthStatus, "CRITICAL") {
		return "CRITICAL"
	}
	if consistency != nil && strings.ToUpper(strings.TrimSpace(consistency.Status)) != "IN_SYNC" {
		return "DESYNCHRONIZED"
	}
	if strings.EqualFold(freshness.FreshnessStatus, "VERY_STALE") {
		return "STALE"
	}
	if strings.EqualFold(health.HealthStatus, "DEGRADED") || strings.EqualFold(freshness.FreshnessStatus, "STALE") {
		return "DEGRADED"
	}
	if strings.EqualFold(health.Readiness, "READY") && strings.EqualFold(freshness.ReadinessImpact, "READY") {
		return "READY"
	}
	return "NOT_READY"
}

func (s *MissionOperationsService) Timeline(ctx context.Context, missionID string, limit int) ([]MissionTimelineEntry, error) {
	missionID = strings.TrimSpace(missionID)
	if missionID == "" {
		return nil, fmt.Errorf("missionID is required")
	}
	if s == nil || s.repo == nil {
		return nil, fmt.Errorf("mission operations repository is not configured")
	}
	if limit <= 0 {
		limit = 50
	}
	if limit > 200 {
		limit = 200
	}
	records, err := s.repo.Query(ctx, RecordQuery{MissionID: missionID, Limit: limit})
	if err != nil {
		return nil, err
	}
	out := make([]MissionTimelineEntry, 0, len(records))
	for _, record := range records {
		out = append(out, timelineEntryFromRecord(record))
	}
	return out, nil
}

func timelineEntryFromRecord(record GroundRecord) MissionTimelineEntry {
	entry := MissionTimelineEntry{
		RecordID:           record.Envelope.RecordID,
		Kind:               record.Envelope.Kind,
		SourceNode:         record.Envelope.SourceNode,
		MissionTimestampNS: record.Envelope.MissionTimestamp,
		SequenceNumber:     record.Envelope.SequenceNumber,
		Status:             "RECORDED",
	}
	switch record.Envelope.Kind {
	case KindTelemetry:
		metric := strings.TrimSpace(record.Fields["metric"])
		value := strings.TrimSpace(record.Fields["value"])
		unit := strings.TrimSpace(record.Fields["unit"])
		if metric == "" {
			metric = "telemetry"
		}
		entry.Summary = metric
		if value != "" {
			entry.Summary += "=" + value
		}
		if unit != "" {
			entry.Summary += " " + unit
		}
	case KindScience:
		instrument := strings.TrimSpace(record.Fields["instrument"])
		target := strings.TrimSpace(record.Fields["target"])
		measurement := strings.TrimSpace(record.Fields["measurement"])
		entry.Summary = strings.TrimSpace(strings.Join([]string{instrument, target, measurement}, ":"))
	case KindEvent:
		eventType := strings.TrimSpace(record.Fields["event_type"])
		entry.Severity = strings.ToUpper(strings.TrimSpace(record.Fields["severity"]))
		entry.Lifecycle = strings.ToUpper(strings.TrimSpace(record.Fields["lifecycle"]))
		if entry.Lifecycle == "" {
			entry.Lifecycle = string(EventActive)
		}
		entry.Summary = eventType
		if entry.Severity != "" {
			entry.Summary += " [" + entry.Severity + "]"
		}
	case KindCommand:
		command := strings.ToUpper(strings.TrimSpace(record.Fields["command"]))
		target := strings.TrimSpace(record.Fields["target"])
		entry.Lifecycle = strings.ToUpper(strings.TrimSpace(record.Fields["lifecycle"]))
		if entry.Lifecycle == "" {
			entry.Lifecycle = string(CommandValidated)
		}
		entry.Summary = command
		if target != "" {
			entry.Summary += " -> " + target
		}
	case KindFile:
		entry.Summary = strings.TrimSpace(record.Fields["filename"])
		if size := strings.TrimSpace(record.Fields["size_bytes"]); size != "" {
			if n, err := strconv.ParseUint(size, 10, 64); err == nil {
				entry.Summary += fmt.Sprintf(" (%d bytes)", n)
			}
		}
	default:
		entry.Summary = string(record.Envelope.Kind)
	}
	return entry
}

func (s *MissionOperationsService) commandOperations(ctx context.Context, missionID string) (MissionCommandOperations, error) {
	records, err := s.repo.Query(ctx, RecordQuery{MissionID: missionID, Kind: KindCommand, Limit: 1000})
	if err != nil {
		return MissionCommandOperations{}, err
	}
	latestByID := make(map[string]MissionCommandState)
	for _, record := range records {
		id := resolveCommandID(record)
		if id == "" {
			continue
		}
		lifecycle := strings.ToUpper(strings.TrimSpace(record.Fields["lifecycle"]))
		if lifecycle == "" {
			lifecycle = string(CommandValidated)
		}
		state := MissionCommandState{
			CommandID: id,
			Command:   strings.ToUpper(strings.TrimSpace(record.Fields["command"])),
			Target:    strings.TrimSpace(record.Fields["target"]),
			Lifecycle: lifecycle,
			Terminal:  lifecycle == string(CommandCompleted) || lifecycle == string(CommandFailed) || lifecycle == string(CommandRejected),
			Sequence:  record.Envelope.SequenceNumber,
		}
		if existing, ok := latestByID[id]; ok && existing.Sequence >= state.Sequence {
			continue
		}
		latestByID[id] = state
	}
	values := make([]MissionCommandState, 0, len(latestByID))
	for _, state := range latestByID {
		values = append(values, state)
	}
	sort.Slice(values, func(i, j int) bool {
		if values[i].Sequence != values[j].Sequence {
			return values[i].Sequence > values[j].Sequence
		}
		return values[i].CommandID < values[j].CommandID
	})
	summary := MissionCommandOperations{UniqueCommands: len(values)}
	for _, state := range values {
		switch state.Lifecycle {
		case string(CommandCompleted):
			summary.CompletedCommands++
		case string(CommandFailed):
			summary.FailedCommands++
		case string(CommandRejected):
			summary.RejectedCommands++
		default:
			summary.ActiveCommands++
		}
	}
	if len(values) > 20 {
		summary.Latest = append([]MissionCommandState(nil), values[:20]...)
	} else {
		summary.Latest = values
	}
	return summary, nil
}
