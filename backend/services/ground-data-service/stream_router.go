package main

import (
	"context"
	"fmt"
	"strconv"
	"strings"
	"sync"
	"time"
)

// RouteSnapshot exposes the per-kind processing pipeline counters and the
// latest domain-specific operation performed for each pipeline.
type RouteSnapshot struct {
	Telemetry RoutedPipelineSnapshot `json:"telemetry"`
	Science   RoutedPipelineSnapshot `json:"science"`
	Event     RoutedPipelineSnapshot `json:"event"`
	Command   RoutedPipelineSnapshot `json:"command"`
	File      RoutedPipelineSnapshot `json:"file"`
}

type RoutedPipelineSnapshot struct {
	Received      uint64            `json:"received"`
	Accepted      uint64            `json:"accepted"`
	Rejected      uint64            `json:"rejected"`
	LastRecordID  string            `json:"last_record_id,omitempty"`
	LastSequence  uint64            `json:"last_sequence,omitempty"`
	LastError     string            `json:"last_error,omitempty"`
	LastOperation string            `json:"last_operation,omitempty"`
	LastResult    *ProcessingResult `json:"last_result,omitempty"`
}

// RecordRouteHandler is the processing contract for a routed GroundRecord.
// V0.9.64 supplied routing. V0.9.65 adds domain-specific processing behind
// the same interface so handlers can later become dedicated Kafka services.
type RecordRouteHandler interface {
	Handle(GroundRecord) error
}

type RoutedPipeline struct {
	mu      sync.RWMutex
	snap    RoutedPipelineSnapshot
	handler OperationHandler
}

func NewRoutedPipeline(handler OperationHandler) *RoutedPipeline {
	return &RoutedPipeline{handler: handler}
}

func (p *RoutedPipeline) Process(record GroundRecord) error {
	p.mu.Lock()
	p.snap.Received++
	p.mu.Unlock()

	if p.handler == nil {
		p.reject(record, "pipeline handler is not configured")
		return fmt.Errorf("pipeline handler is not configured")
	}
	result, err := p.handler.ProcessOutput(record)
	if err != nil {
		p.reject(record, err.Error())
		return err
	}

	p.mu.Lock()
	p.snap.Accepted++
	p.snap.LastRecordID = record.Envelope.RecordID
	p.snap.LastSequence = record.Envelope.SequenceNumber
	p.snap.LastError = ""
	p.snap.LastOperation = result.Operation
	p.snap.LastResult = &result
	p.mu.Unlock()
	return nil
}

func (p *RoutedPipeline) reject(record GroundRecord, message string) {
	p.mu.Lock()
	defer p.mu.Unlock()
	p.snap.Rejected++
	p.snap.LastRecordID = record.Envelope.RecordID
	p.snap.LastSequence = record.Envelope.SequenceNumber
	p.snap.LastError = message
	p.snap.LastOperation = ""
}

func (p *RoutedPipeline) Snapshot() RoutedPipelineSnapshot {
	p.mu.RLock()
	defer p.mu.RUnlock()
	return p.snap
}

// OperationHandler extends the older Handle contract with a useful domain
// operation description for observability. Handle remains available to make
// the routing package easy to reuse in future services.
type OperationHandler interface {
	RecordRouteHandler
	HandleOperation(GroundRecord) (string, error)
	ProcessOutput(GroundRecord) (ProcessingResult, error)
}

func (p *RoutedPipeline) String() string { return "routed pipeline" }

type domainHandler struct {
	kind          DataKind
	eventEngine   *EventEngine
	commandEngine *CommandEngine
}

func (h domainHandler) Handle(record GroundRecord) error {
	_, err := h.HandleOperation(record)
	return err
}

func (h domainHandler) HandleOperation(record GroundRecord) (string, error) {
	result, err := h.ProcessOutput(record)
	if err != nil {
		return "", err
	}
	return result.Operation, nil
}

func (h domainHandler) ProcessOutput(record GroundRecord) (ProcessingResult, error) {
	switch h.kind {
	case KindTelemetry:
		return processTelemetryOutput(record)
	case KindScience:
		return processScienceOutput(record)
	case KindEvent:
		return processEventEngineOutput(record, h.eventEngine)
	case KindCommand:
		return processCommandEngineOutput(record, h.commandEngine)
	case KindFile:
		return processFileOutput(record)
	default:
		return ProcessingResult{}, fmt.Errorf("unsupported domain kind %q", h.kind)
	}
}

func requiredField(fields map[string]string, name string) (string, error) {
	value := strings.TrimSpace(fields[name])
	if value == "" {
		return "", fmt.Errorf("required field %q is missing", name)
	}
	return value, nil
}

func processTelemetryRecord(record GroundRecord) (string, error) {
	if record.Envelope.Kind != KindTelemetry {
		return "", fmt.Errorf("telemetry handler received %q", record.Envelope.Kind)
	}
	if _, err := requiredField(record.Fields, "value"); err != nil {
		// Telemetry may use a named metric/value pair, or a compact payload.
		// Accept well-formed multi-field telemetry even when metric is omitted.
		if len(record.Fields) == 0 {
			return "", fmt.Errorf("telemetry payload is empty")
		}
	}
	metric := strings.TrimSpace(record.Fields["metric"])
	if metric == "" {
		metric = "multi-field"
	}
	return "telemetry-normalize:" + metric, nil
}

func processScienceRecord(record GroundRecord) (string, error) {
	if record.Envelope.Kind != KindScience {
		return "", fmt.Errorf("science handler received %q", record.Envelope.Kind)
	}
	instrument, err := requiredField(record.Fields, "instrument")
	if err != nil {
		return "", err
	}
	target, err := requiredField(record.Fields, "target")
	if err != nil {
		return "", err
	}
	return "science-validate:" + instrument + ":" + target, nil
}

func processEventRecord(record GroundRecord) (string, error) {
	if record.Envelope.Kind != KindEvent {
		return "", fmt.Errorf("event handler received %q", record.Envelope.Kind)
	}
	eventType, err := requiredField(record.Fields, "event_type")
	if err != nil {
		return "", err
	}
	severity, err := requiredField(record.Fields, "severity")
	if err != nil {
		return "", err
	}
	return "event-classify:" + strings.ToUpper(severity) + ":" + eventType, nil
}

func processCommandRecord(record GroundRecord) (string, error) {
	if record.Envelope.Kind != KindCommand {
		return "", fmt.Errorf("command handler received %q", record.Envelope.Kind)
	}
	command, err := requiredField(record.Fields, "command")
	if err != nil {
		return "", err
	}
	target, err := requiredField(record.Fields, "target")
	if err != nil {
		return "", err
	}
	return "command-validate:" + strings.ToUpper(command) + ":" + target, nil
}

func processFileRecord(record GroundRecord) (string, error) {
	if record.Envelope.Kind != KindFile {
		return "", fmt.Errorf("file handler received %q", record.Envelope.Kind)
	}
	filename, err := requiredField(record.Fields, "filename")
	if err != nil {
		return "", err
	}
	if sizeRaw, ok := record.Fields["size_bytes"]; ok && strings.TrimSpace(sizeRaw) != "" {
		if _, err := strconv.ParseUint(strings.TrimSpace(sizeRaw), 10, 64); err != nil {
			return "", fmt.Errorf("invalid size_bytes: %w", err)
		}
	}
	return "file-metadata:" + filename, nil
}

// RecordRouter routes validated Kafka records to a dedicated per-kind pipeline.
type RecordRouter struct {
	pipelines         map[DataKind]*RoutedPipeline
	commandEngine     *CommandEngine
	commandReconciler *CommandExecutionReconciler
	missionState      *MissionStateEngine
}

func NewRecordRouter() *RecordRouter {
	eventEngine := NewEventEngine()
	commandEngine := NewCommandEngine()
	return &RecordRouter{
		commandEngine:     commandEngine,
		commandReconciler: NewCommandExecutionReconciler(),
		missionState:      NewMissionStateEngine(),
		pipelines: map[DataKind]*RoutedPipeline{
			KindTelemetry: NewRoutedPipeline(domainHandler{kind: KindTelemetry}),
			KindScience:   NewRoutedPipeline(domainHandler{kind: KindScience}),
			KindEvent:     NewRoutedPipeline(domainHandler{kind: KindEvent, eventEngine: eventEngine}),
			KindCommand:   NewRoutedPipeline(domainHandler{kind: KindCommand, commandEngine: commandEngine}),
			KindFile:      NewRoutedPipeline(domainHandler{kind: KindFile}),
		},
	}
}

func (r *RecordRouter) Route(record GroundRecord) error {
	pipeline, ok := r.pipelines[record.Envelope.Kind]
	if !ok {
		return fmt.Errorf("unsupported record kind %q", record.Envelope.Kind)
	}
	if err := pipeline.Process(record); err != nil {
		return err
	}
	if r.missionState != nil {
		snapshot := pipeline.Snapshot()
		if snapshot.LastResult != nil {
			if err := r.missionState.Apply(*snapshot.LastResult); err != nil {
				return err
			}
		}
	}
	if r.commandReconciler != nil {
		// Commands and vehicle feedback (event/telemetry) can advance the
		// execution lifecycle. Unrelated records are ignored. An invalid
		// command lifecycle is rejected; malformed/unrelated feedback is
		// retained by its domain pipeline while the reconciliation error is
		// observable through the command reconciliation API.
		if err := r.commandReconciler.Apply(record); err != nil && record.Envelope.Kind == KindCommand {
			return err
		}
	}
	return nil
}

func (r *RecordRouter) Snapshot() RouteSnapshot {
	return RouteSnapshot{
		Telemetry: r.pipelines[KindTelemetry].Snapshot(),
		Science:   r.pipelines[KindScience].Snapshot(),
		Event:     r.pipelines[KindEvent].Snapshot(),
		Command:   r.pipelines[KindCommand].Snapshot(),
		File:      r.pipelines[KindFile].Snapshot(),
	}
}

// ProcessingResults returns the latest structured processing result for each
// domain. Keeping this projection small makes it suitable for an operator/API
// surface now and for later publication to dedicated downstream topics.

// CommandLifecycle returns the latest lifecycle state tracked by the command
// domain processor.

// MissionState exposes the derived mission-operational state assembled from
// successful domain processing results.
func (r *RecordRouter) MissionState() map[string]MissionState {
	if r == nil || r.missionState == nil {
		return map[string]MissionState{}
	}
	return r.missionState.Snapshot()
}

func (r *RecordRouter) VehicleSummaries(missionID string) []VehicleStateSummary {
	if r == nil || r.missionState == nil {
		return []VehicleStateSummary{}
	}
	return r.missionState.VehicleSummaries(missionID)
}

func (r *RecordRouter) Subsystems(missionID, sourceNode string) []SubsystemStateSummary {
	if r == nil || r.missionState == nil {
		return []SubsystemStateSummary{}
	}
	return r.missionState.SubsystemSummaries(missionID, sourceNode)
}

func (r *RecordRouter) Subsystem(missionID, sourceNode, subsystem string) (SubsystemStateSummary, bool) {
	if r == nil || r.missionState == nil {
		return SubsystemStateSummary{}, false
	}
	return r.missionState.Subsystem(missionID, sourceNode, subsystem)
}

func (r *RecordRouter) Vehicle(missionID, sourceNode string) (VehicleStateSummary, bool) {
	if r == nil || r.missionState == nil {
		return VehicleStateSummary{}, false
	}
	return r.missionState.Vehicle(missionID, sourceNode)
}

func (r *RecordRouter) MissionFreshness(missionID string, now time.Time, warnAfter, criticalAfter time.Duration) (MissionFreshnessSummary, bool, error) {
	if r == nil || r.missionState == nil {
		return MissionFreshnessSummary{}, false, nil
	}
	return r.missionState.Freshness(missionID, now, warnAfter, criticalAfter)
}

func (r *RecordRouter) MissionHealth(missionID string) (MissionHealthSummary, bool) {
	if r == nil || r.missionState == nil {
		return MissionHealthSummary{}, false
	}
	return r.missionState.Health(missionID)
}

func (r *RecordRouter) MissionStateConsistency(ctx context.Context, repo RecordRepository, missionID string, checkedAt time.Time) (MissionStateConsistency, bool, error) {
	if r == nil || r.missionState == nil {
		return MissionStateConsistency{}, false, nil
	}
	return r.missionState.CheckConsistency(ctx, repo, missionID, checkedAt)
}

func (r *RecordRouter) MissionHealthState(missionID string, now time.Time, warnAfter, criticalAfter time.Duration) (MissionHealthState, bool, error) {
	if r == nil || r.missionState == nil {
		return MissionHealthState{}, false, nil
	}
	return r.missionState.HealthState(missionID, now, warnAfter, criticalAfter)
}

func (r *RecordRouter) Mission(missionID string) (MissionState, bool) {
	if r == nil || r.missionState == nil {
		return MissionState{}, false
	}
	return r.missionState.Get(missionID)
}

func (r *RecordRouter) CommandLifecycle() []CommandLifecycleEvaluation {
	if r == nil || r.commandEngine == nil {
		return []CommandLifecycleEvaluation{}
	}
	return r.commandEngine.Snapshot()
}

func (r *RecordRouter) CommandExecution() []CommandExecutionReconciliation {
	if r == nil || r.commandReconciler == nil {
		return []CommandExecutionReconciliation{}
	}
	return r.commandReconciler.Snapshot()
}

func (r *RecordRouter) CommandExecutionByID(commandID string) (CommandExecutionReconciliation, bool) {
	if r == nil || r.commandReconciler == nil {
		return CommandExecutionReconciliation{}, false
	}
	return r.commandReconciler.Get(commandID)
}

func (r *RecordRouter) CommandExecutionForMission(missionID string) []CommandExecutionReconciliation {
	if r == nil || r.commandReconciler == nil {
		return []CommandExecutionReconciliation{}
	}
	return r.commandReconciler.ForMission(missionID)
}

func (r *RecordRouter) Results() map[DataKind]ProcessingResult {
	results := make(map[DataKind]ProcessingResult, len(r.pipelines))
	for kind, pipeline := range r.pipelines {
		snapshot := pipeline.Snapshot()
		if snapshot.LastResult != nil {
			results[kind] = *snapshot.LastResult
		}
	}
	return results
}
