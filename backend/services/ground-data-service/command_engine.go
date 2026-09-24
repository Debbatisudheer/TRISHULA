package main

import (
	"fmt"
	"strings"
	"sync"
)

// CommandLifecycle describes where a ground command currently sits in its
// operational lifecycle. The engine tracks the lifecycle of a command as
// records arrive from the command/telemetry feedback path; it does not execute
// vehicle commands itself.
type CommandLifecycle string

const (
	CommandReceived     CommandLifecycle = "RECEIVED"
	CommandValidated    CommandLifecycle = "VALIDATED"
	CommandQueued       CommandLifecycle = "QUEUED"
	CommandSent         CommandLifecycle = "SENT"
	CommandAcknowledged CommandLifecycle = "ACKNOWLEDGED"
	CommandExecuting    CommandLifecycle = "EXECUTING"
	CommandCompleted    CommandLifecycle = "COMPLETED"
	CommandFailed       CommandLifecycle = "FAILED"
	CommandRejected     CommandLifecycle = "REJECTED"
	CommandTimeout      CommandLifecycle = "TIMEOUT"
	CommandCancelled    CommandLifecycle = "CANCELLED"
)

type CommandLifecycleEvaluation struct {
	CommandID  string
	Command    string
	Target     string
	Lifecycle  CommandLifecycle
	Previous   CommandLifecycle
	Transition string
	Terminal   bool
	Sequence   uint64
}

// CommandEngine keeps the latest lifecycle state for each command record ID.
// It is intentionally independent from PostgreSQL/Redis so the lifecycle
// policy can later move behind a dedicated command-service boundary.
type CommandEngine struct {
	mu       sync.RWMutex
	commands map[string]CommandLifecycleEvaluation
}

func NewCommandEngine() *CommandEngine {
	return &CommandEngine{commands: make(map[string]CommandLifecycleEvaluation)}
}

var defaultCommandEngine = NewCommandEngine()

func normalizeCommandLifecycle(raw string) (CommandLifecycle, error) {
	value := strings.ToUpper(strings.TrimSpace(raw))
	if value == "" {
		return CommandValidated, nil
	}
	switch CommandLifecycle(value) {
	case CommandReceived,
		CommandValidated,
		CommandQueued,
		CommandSent,
		CommandAcknowledged,
		CommandExecuting,
		CommandCompleted,
		CommandFailed,
		CommandRejected,
		CommandTimeout,
		CommandCancelled:
		return CommandLifecycle(value), nil
	default:
		return "", fmt.Errorf("unsupported command lifecycle %q", raw)
	}
}

func lifecycleTerminal(state CommandLifecycle) bool {
	return state == CommandCompleted || state == CommandFailed || state == CommandRejected || state == CommandTimeout || state == CommandCancelled
}

func lifecycleRank(state CommandLifecycle) int {
	switch state {
	case CommandReceived:
		return 1
	case CommandValidated:
		return 2
	case CommandQueued:
		return 3
	case CommandSent:
		return 4
	case CommandAcknowledged:
		return 5
	case CommandExecuting:
		return 6
	case CommandCompleted, CommandFailed, CommandRejected, CommandTimeout, CommandCancelled:
		return 7
	default:
		return 0
	}
}

func isValidCommandTransition(previous, next CommandLifecycle) bool {
	if previous == "" {
		return next == CommandReceived || next == CommandValidated || next == CommandRejected
	}
	if previous == next {
		return true
	}
	if lifecycleTerminal(previous) {
		return false
	}
	switch previous {
	case CommandReceived:
		return next == CommandValidated || next == CommandRejected
	case CommandValidated:
		return next == CommandQueued || next == CommandSent || next == CommandRejected || next == CommandCancelled
	case CommandQueued:
		return next == CommandSent || next == CommandRejected || next == CommandTimeout || next == CommandCancelled
	case CommandSent:
		return next == CommandAcknowledged || next == CommandFailed || next == CommandTimeout || next == CommandCancelled
	case CommandAcknowledged:
		return next == CommandExecuting || next == CommandFailed || next == CommandTimeout || next == CommandCancelled
	case CommandExecuting:
		return next == CommandCompleted || next == CommandFailed || next == CommandTimeout || next == CommandCancelled
	default:
		return false
	}
}

func resolveCommandID(record GroundRecord) string {
	if value := strings.TrimSpace(record.Fields["command_id"]); value != "" {
		return value
	}
	if value := strings.TrimSpace(record.Envelope.CorrelationID); value != "" {
		return value
	}
	return strings.TrimSpace(record.Envelope.RecordID)
}

func (e *CommandEngine) Evaluate(record GroundRecord) (CommandLifecycleEvaluation, error) {
	if record.Envelope.Kind != KindCommand {
		return CommandLifecycleEvaluation{}, fmt.Errorf("command engine received %q", record.Envelope.Kind)
	}
	command, err := requiredField(record.Fields, "command")
	if err != nil {
		return CommandLifecycleEvaluation{}, err
	}
	target, err := requiredField(record.Fields, "target")
	if err != nil {
		return CommandLifecycleEvaluation{}, err
	}
	state, err := normalizeCommandLifecycle(record.Fields["lifecycle"])
	if err != nil {
		return CommandLifecycleEvaluation{}, err
	}
	id := resolveCommandID(record)
	if id == "" {
		return CommandLifecycleEvaluation{}, fmt.Errorf("command_id or correlation_id or record_id is required")
	}

	e.mu.Lock()
	defer e.mu.Unlock()

	previous := CommandLifecycle("")
	if existing, ok := e.commands[id]; ok {
		previous = existing.Lifecycle
	}
	if !isValidCommandTransition(previous, state) {
		return CommandLifecycleEvaluation{}, fmt.Errorf("invalid command lifecycle transition %q -> %q", previous, state)
	}

	evaluation := CommandLifecycleEvaluation{
		CommandID: id,
		Command:   strings.ToUpper(strings.TrimSpace(command)),
		Target:    target,
		Lifecycle: state,
		Previous:  previous,
		Terminal:  lifecycleTerminal(state),
		Sequence:  record.Envelope.SequenceNumber,
	}
	if previous == "" {
		evaluation.Transition = "->" + string(state)
	} else if previous == state {
		evaluation.Transition = string(previous) + "->" + string(state) + ":NOOP"
	} else {
		evaluation.Transition = string(previous) + "->" + string(state)
	}
	_ = lifecycleRank(state)
	e.commands[id] = evaluation
	return evaluation, nil
}

func (e *CommandEngine) Snapshot() []CommandLifecycleEvaluation {
	e.mu.RLock()
	defer e.mu.RUnlock()
	values := make([]CommandLifecycleEvaluation, 0, len(e.commands))
	for _, value := range e.commands {
		values = append(values, value)
	}
	return values
}

// processCommandSnapshotOutput reconstructs a persisted command lifecycle
// snapshot for mission-state/read-model rebuilds. It deliberately does not
// consult or mutate the live CommandEngine transition state. A durable record
// may legitimately contain COMPLETED/FAILED/REJECTED as its latest snapshot,
// even when earlier lifecycle records are not present in the retained data.
func processCommandSnapshotOutput(record GroundRecord) (ProcessingResult, error) {
	if record.Envelope.Kind != KindCommand {
		return ProcessingResult{}, fmt.Errorf("command snapshot processor received %q", record.Envelope.Kind)
	}
	command, err := requiredField(record.Fields, "command")
	if err != nil {
		return ProcessingResult{}, err
	}
	target, err := requiredField(record.Fields, "target")
	if err != nil {
		return ProcessingResult{}, err
	}
	lifecycle, err := normalizeCommandLifecycle(record.Fields["lifecycle"])
	if err != nil {
		return ProcessingResult{}, err
	}
	id := resolveCommandID(record)
	if id == "" {
		return ProcessingResult{}, fmt.Errorf("command_id or correlation_id or record_id is required")
	}
	operation := fmt.Sprintf("command-snapshot:%s:%s", strings.ToUpper(strings.TrimSpace(command)), lifecycle)
	attributes := map[string]string{
		"command_id":     id,
		"command":        strings.ToUpper(strings.TrimSpace(command)),
		"target":         target,
		"lifecycle":      string(lifecycle),
		"previous_state": "",
		"transition":     "SNAPSHOT->" + string(lifecycle),
		"terminal":       fmt.Sprintf("%t", lifecycleTerminal(lifecycle)),
		"lifecycle_rank": fmt.Sprintf("%d", lifecycleRank(lifecycle)),
	}
	if reason := strings.TrimSpace(record.Fields["reason"]); reason != "" {
		attributes["reason"] = reason
	}
	return buildProcessingResult(record, operation, attributes), nil
}

func processCommandEngineOutput(record GroundRecord, engine *CommandEngine) (ProcessingResult, error) {
	if engine == nil {
		engine = defaultCommandEngine
	}
	evaluation, err := engine.Evaluate(record)
	if err != nil {
		return ProcessingResult{}, err
	}
	operation := fmt.Sprintf("command-lifecycle:%s:%s", evaluation.Command, evaluation.Lifecycle)
	attributes := map[string]string{
		"command_id":     evaluation.CommandID,
		"command":        evaluation.Command,
		"target":         evaluation.Target,
		"lifecycle":      string(evaluation.Lifecycle),
		"previous_state": string(evaluation.Previous),
		"transition":     evaluation.Transition,
		"terminal":       fmt.Sprintf("%t", evaluation.Terminal),
		"lifecycle_rank": fmt.Sprintf("%d", lifecycleRank(evaluation.Lifecycle)),
	}
	if reason := strings.TrimSpace(record.Fields["reason"]); reason != "" {
		attributes["reason"] = reason
	}
	return buildProcessingResult(record, operation, attributes), nil
}
