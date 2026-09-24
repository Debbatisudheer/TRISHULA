package main

import (
	"fmt"
	"sort"
	"strings"
	"sync"
	"time"
)

// CommandExecutionState is the mission-operation lifecycle used to reconcile
// a ground command with acknowledgements and vehicle execution feedback.
type CommandExecutionState string

const (
	CommandCreated          CommandExecutionState = "CREATED"
	CommandValidatedExec    CommandExecutionState = "VALIDATED"
	CommandExecutionQueued  CommandExecutionState = "QUEUED"
	CommandSentExec         CommandExecutionState = "SENT"
	CommandAcknowledgedExec CommandExecutionState = "ACKNOWLEDGED"
	CommandExecutingExec    CommandExecutionState = "EXECUTING"
	CommandCompletedExec    CommandExecutionState = "COMPLETED"
	CommandFailedExec       CommandExecutionState = "FAILED"
	CommandRejectedExec     CommandExecutionState = "REJECTED"
	CommandTimeoutExec      CommandExecutionState = "TIMEOUT"
	CommandCancelledExec    CommandExecutionState = "CANCELLED"
)

const currentCommandReconciliationVersion = "v0.9.81"

type CommandExecutionReconciliation struct {
	CommandID             string                `json:"command_id"`
	Command               string                `json:"command"`
	Target                string                `json:"target"`
	MissionID             string                `json:"mission_id"`
	VehicleResponse       string                `json:"vehicle_response,omitempty"`
	State                 CommandExecutionState `json:"state"`
	PreviousState         CommandExecutionState `json:"previous_state,omitempty"`
	Transition            string                `json:"transition"`
	Terminal              bool                  `json:"terminal"`
	ExecutionSequence     uint64                `json:"execution_sequence"`
	AckSequence           uint64                `json:"ack_sequence,omitempty"`
	StartSequence         uint64                `json:"start_sequence,omitempty"`
	CompletionSequence    uint64                `json:"completion_sequence,omitempty"`
	SentAt                time.Time             `json:"sent_at,omitempty"`
	AcknowledgedAt        time.Time             `json:"acknowledged_at,omitempty"`
	StartedAt             time.Time             `json:"started_at,omitempty"`
	CompletedAt           time.Time             `json:"completed_at,omitempty"`
	Result                string                `json:"result,omitempty"`
	ErrorCode             string                `json:"error_code,omitempty"`
	LastFeedbackRecordID  string                `json:"last_feedback_record_id,omitempty"`
	LastFeedbackAt        time.Time             `json:"last_feedback_at,omitempty"`
	ReconciliationVersion string                `json:"reconciliation_version"`
	LastError             string                `json:"last_error,omitempty"`
}

type CommandExecutionReconciler struct {
	mu       sync.RWMutex
	commands map[string]CommandExecutionReconciliation
}

func NewCommandExecutionReconciler() *CommandExecutionReconciler {
	return &CommandExecutionReconciler{commands: make(map[string]CommandExecutionReconciliation)}
}

func isCommandExecutionTerminal(state CommandExecutionState) bool {
	switch state {
	case CommandCompletedExec, CommandFailedExec, CommandRejectedExec, CommandTimeoutExec, CommandCancelledExec:
		return true
	default:
		return false
	}
}

func commandExecutionRank(state CommandExecutionState) int {
	switch state {
	case CommandCreated:
		return 1
	case CommandValidatedExec:
		return 2
	case CommandExecutionQueued:
		return 3
	case CommandSentExec:
		return 4
	case CommandAcknowledgedExec:
		return 5
	case CommandExecutingExec:
		return 6
	case CommandCompletedExec, CommandFailedExec, CommandRejectedExec, CommandTimeoutExec, CommandCancelledExec:
		return 7
	default:
		return 0
	}
}

func normalizeCommandExecutionState(raw string) (CommandExecutionState, error) {
	value := strings.ToUpper(strings.TrimSpace(raw))
	if value == "" {
		return CommandValidatedExec, nil
	}
	// Accept the existing command-engine lifecycle names and the new
	// mission-operations states without requiring callers to migrate at once.
	switch CommandExecutionState(value) {
	case CommandCreated, CommandValidatedExec, CommandExecutionQueued, CommandSentExec,
		CommandAcknowledgedExec, CommandExecutingExec, CommandCompletedExec,
		CommandFailedExec, CommandRejectedExec, CommandTimeoutExec, CommandCancelledExec:
		return CommandExecutionState(value), nil
	default:
		return "", fmt.Errorf("unsupported command execution state %q", raw)
	}
}

func validCommandExecutionTransition(previous, next CommandExecutionState) bool {
	if previous == "" {
		switch next {
		case CommandCreated, CommandValidatedExec, CommandExecutionQueued, CommandSentExec,
			CommandAcknowledgedExec, CommandExecutingExec, CommandCompletedExec,
			CommandFailedExec, CommandRejectedExec, CommandTimeoutExec, CommandCancelledExec:
			return true
		default:
			return false
		}
	}
	if previous == next {
		return true
	}
	if isCommandExecutionTerminal(previous) {
		return false
	}
	switch previous {
	case CommandCreated:
		return next == CommandValidatedExec || next == CommandExecutionQueued || next == CommandRejectedExec || next == CommandCancelledExec
	case CommandValidatedExec:
		return next == CommandExecutionQueued || next == CommandSentExec || next == CommandRejectedExec || next == CommandCancelledExec
	case CommandExecutionQueued:
		return next == CommandSentExec || next == CommandRejectedExec || next == CommandCancelledExec || next == CommandTimeoutExec
	case CommandSentExec:
		return next == CommandAcknowledgedExec || next == CommandFailedExec || next == CommandTimeoutExec || next == CommandCancelledExec
	case CommandAcknowledgedExec:
		return next == CommandExecutingExec || next == CommandFailedExec || next == CommandTimeoutExec || next == CommandCancelledExec
	case CommandExecutingExec:
		return next == CommandCompletedExec || next == CommandFailedExec || next == CommandTimeoutExec || next == CommandCancelledExec
	default:
		return false
	}
}

func feedbackCommandID(record GroundRecord) string {
	if value := strings.TrimSpace(record.Fields["command_id"]); value != "" {
		return value
	}
	if value := strings.TrimSpace(record.Envelope.CorrelationID); value != "" {
		return value
	}
	return ""
}

func feedbackStateFromRecord(record GroundRecord) (CommandExecutionState, bool, error) {
	raw := strings.TrimSpace(record.Fields["execution_state"])
	if raw == "" {
		raw = strings.TrimSpace(record.Fields["command_status"])
	}
	if raw == "" {
		raw = strings.TrimSpace(record.Fields["lifecycle"])
	}
	if raw != "" {
		// RECEIVED is the operator-facing command lifecycle entry point. It is
		// intentionally handled by the orchestrator boundary, not the execution
		// reconciliation state machine. Do not reject the Kafka command before
		// the orchestrator gets a chance to enqueue it.
		if strings.EqualFold(raw, string(CommandReceived)) {
			return "", false, nil
		}
		state, err := normalizeCommandExecutionState(raw)
		return state, true, err
	}
	eventType := strings.ToUpper(strings.TrimSpace(record.Fields["event_type"]))
	switch eventType {
	case "COMMAND_CREATED":
		return CommandCreated, true, nil
	case "COMMAND_VALIDATED":
		return CommandValidatedExec, true, nil
	case "COMMAND_QUEUED":
		return CommandExecutionQueued, true, nil
	case "COMMAND_SENT":
		return CommandSentExec, true, nil
	case "COMMAND_ACK", "COMMAND_ACKNOWLEDGED":
		return CommandAcknowledgedExec, true, nil
	case "COMMAND_EXECUTING":
		return CommandExecutingExec, true, nil
	case "COMMAND_COMPLETED", "COMMAND_COMPLETE":
		return CommandCompletedExec, true, nil
	case "COMMAND_FAILED":
		return CommandFailedExec, true, nil
	case "COMMAND_REJECTED":
		return CommandRejectedExec, true, nil
	case "COMMAND_TIMEOUT", "COMMAND_TIMED_OUT":
		return CommandTimeoutExec, true, nil
	case "COMMAND_CANCELLED", "COMMAND_CANCELED":
		return CommandCancelledExec, true, nil
	default:
		return "", false, nil
	}
}

func (r *CommandExecutionReconciler) Apply(record GroundRecord) error {
	if r == nil {
		return fmt.Errorf("command execution reconciler is nil")
	}
	if record.Envelope.Kind != KindCommand && record.Envelope.Kind != KindEvent && record.Envelope.Kind != KindTelemetry {
		return nil
	}

	commandID := resolveCommandID(record)
	if record.Envelope.Kind != KindCommand {
		commandID = feedbackCommandID(record)
	}
	if commandID == "" {
		// Unrelated telemetry/events do not participate in command reconciliation.
		return nil
	}

	state, recognized, err := feedbackStateFromRecord(record)
	if err != nil {
		return err
	}
	if !recognized {
		return nil
	}

	command := strings.ToUpper(strings.TrimSpace(record.Fields["command"]))
	target := strings.TrimSpace(record.Fields["target"])
	missionID := strings.TrimSpace(record.Envelope.MissionID)
	now := time.Now().UTC()

	r.mu.Lock()
	defer r.mu.Unlock()

	current, exists := r.commands[commandID]
	previous := CommandExecutionState("")
	if exists {
		previous = current.State
		if command == "" {
			command = current.Command
		}
		if target == "" {
			target = current.Target
		}
		if missionID == "" {
			missionID = current.MissionID
		}
	}

	if !validCommandExecutionTransition(previous, state) {
		if !exists {
			return fmt.Errorf("invalid initial command execution state %q", state)
		}
		current.LastError = fmt.Sprintf("invalid command execution transition %q -> %q", previous, state)
		current.LastFeedbackRecordID = record.Envelope.RecordID
		current.LastFeedbackAt = now
		r.commands[commandID] = current
		return fmt.Errorf("invalid command execution transition %q -> %q", previous, state)
	}

	evaluation := current
	evaluation.CommandID = commandID
	evaluation.Command = command
	evaluation.Target = target
	evaluation.MissionID = missionID
	evaluation.PreviousState = previous
	evaluation.State = state
	evaluation.Transition = transitionName(previous, state)
	evaluation.Terminal = isCommandExecutionTerminal(state)
	evaluation.ExecutionSequence = record.Envelope.SequenceNumber
	evaluation.LastFeedbackRecordID = record.Envelope.RecordID
	evaluation.LastFeedbackAt = now
	evaluation.ReconciliationVersion = currentCommandReconciliationVersion
	evaluation.LastError = ""

	if state == CommandSentExec && evaluation.SentAt.IsZero() {
		evaluation.SentAt = now
	}
	if state == CommandAcknowledgedExec && evaluation.AcknowledgedAt.IsZero() {
		evaluation.AcknowledgedAt = now
		evaluation.AckSequence = record.Envelope.SequenceNumber
	}
	if state == CommandExecutingExec && evaluation.StartedAt.IsZero() {
		evaluation.StartedAt = now
		evaluation.StartSequence = record.Envelope.SequenceNumber
	}
	if evaluation.Terminal {
		if evaluation.CompletedAt.IsZero() {
			evaluation.CompletedAt = now
			evaluation.CompletionSequence = record.Envelope.SequenceNumber
		}
		if result := strings.TrimSpace(record.Fields["result"]); result != "" {
			evaluation.Result = result
		}
		if code := strings.TrimSpace(record.Fields["error_code"]); code != "" {
			evaluation.ErrorCode = code
		}
	}
	if response := strings.TrimSpace(record.Fields["vehicle_response"]); response != "" {
		evaluation.VehicleResponse = response
	} else if response := strings.TrimSpace(record.Fields["response"]); response != "" {
		evaluation.VehicleResponse = response
	}
	r.commands[commandID] = evaluation
	return nil
}

func transitionName(previous, next CommandExecutionState) string {
	if previous == "" {
		return "->" + string(next)
	}
	if previous == next {
		return string(previous) + "->" + string(next) + ":NOOP"
	}
	return string(previous) + "->" + string(next)
}

func (r *CommandExecutionReconciler) Snapshot() []CommandExecutionReconciliation {
	if r == nil {
		return []CommandExecutionReconciliation{}
	}
	r.mu.RLock()
	defer r.mu.RUnlock()
	values := make([]CommandExecutionReconciliation, 0, len(r.commands))
	for _, value := range r.commands {
		values = append(values, value)
	}
	sort.Slice(values, func(i, j int) bool {
		if values[i].LastFeedbackAt.Equal(values[j].LastFeedbackAt) {
			return values[i].CommandID < values[j].CommandID
		}
		return values[i].LastFeedbackAt.After(values[j].LastFeedbackAt)
	})
	return values
}

func (r *CommandExecutionReconciler) Get(commandID string) (CommandExecutionReconciliation, bool) {
	if r == nil {
		return CommandExecutionReconciliation{}, false
	}
	r.mu.RLock()
	defer r.mu.RUnlock()
	value, ok := r.commands[strings.TrimSpace(commandID)]
	return value, ok
}

func (r *CommandExecutionReconciler) ForMission(missionID string) []CommandExecutionReconciliation {
	missionID = strings.TrimSpace(missionID)
	if missionID == "" {
		return []CommandExecutionReconciliation{}
	}
	values := r.Snapshot()
	out := values[:0]
	for _, value := range values {
		if value.MissionID == missionID {
			out = append(out, value)
		}
	}
	return out
}
