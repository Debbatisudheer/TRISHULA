package main

import (
	"fmt"
	"strings"
	"sync"
)

type EventSeverity string

const (
	EventInfo     EventSeverity = "INFO"
	EventWarning  EventSeverity = "WARNING"
	EventError    EventSeverity = "ERROR"
	EventCritical EventSeverity = "CRITICAL"
)

type EventLifecycle string

const (
	EventActive       EventLifecycle = "ACTIVE"
	EventCleared      EventLifecycle = "CLEARED"
	EventAcknowledged EventLifecycle = "ACKNOWLEDGED"
)

type EventEvaluation struct {
	EventType      string
	Severity       EventSeverity
	SeverityRank   int
	Lifecycle      EventLifecycle
	Subsystem      string
	FaultCode      string
	IsFault        bool
	ActiveFaultKey string
}

// EventEngine turns raw event/fault records into normalized operational
// event state. It intentionally keeps the active-fault projection separate
// from PostgreSQL/Redis persistence so a later mission-state service can own it.
type EventEngine struct {
	mu           sync.RWMutex
	activeFaults map[string]EventEvaluation
}

func NewEventEngine() *EventEngine {
	return &EventEngine{activeFaults: make(map[string]EventEvaluation)}
}

var defaultEventEngine = NewEventEngine()

func normalizeEventSeverity(raw string) (EventSeverity, int, error) {
	switch strings.ToUpper(strings.TrimSpace(raw)) {
	case string(EventInfo):
		return EventInfo, 1, nil
	case string(EventWarning):
		return EventWarning, 2, nil
	case string(EventError):
		return EventError, 3, nil
	case string(EventCritical):
		return EventCritical, 4, nil
	default:
		return "", 0, fmt.Errorf("unsupported event severity %q", raw)
	}
}

func normalizeLifecycle(raw string, isRecovery bool) (EventLifecycle, error) {
	value := strings.ToUpper(strings.TrimSpace(raw))
	if value == "" {
		if isRecovery {
			return EventCleared, nil
		}
		return EventActive, nil
	}
	switch EventLifecycle(value) {
	case EventActive, EventCleared, EventAcknowledged:
		return EventLifecycle(value), nil
	default:
		return "", fmt.Errorf("unsupported event lifecycle %q", raw)
	}
}

func (e *EventEngine) Evaluate(record GroundRecord) (EventEvaluation, error) {
	if record.Envelope.Kind != KindEvent {
		return EventEvaluation{}, fmt.Errorf("event engine received %q", record.Envelope.Kind)
	}
	eventType, err := requiredField(record.Fields, "event_type")
	if err != nil {
		return EventEvaluation{}, err
	}
	severity, rank, err := normalizeEventSeverity(record.Fields["severity"])
	if err != nil {
		return EventEvaluation{}, err
	}
	subsystem := strings.TrimSpace(record.Fields["subsystem"])
	faultCode := strings.TrimSpace(record.Fields["fault_code"])
	isFault := strings.EqualFold(strings.TrimSpace(record.Fields["event_class"]), "FAULT") || faultCode != ""
	if faultCode == "" && isFault {
		faultCode = eventType
	}
	isRecovery := strings.EqualFold(eventType, "RECOVERY") || strings.EqualFold(strings.TrimSpace(record.Fields["event_class"]), "RECOVERY") || strings.TrimSpace(record.Fields["recovery_of"]) != ""
	lifecycle, err := normalizeLifecycle(record.Fields["lifecycle"], isRecovery)
	if err != nil {
		return EventEvaluation{}, err
	}
	key := strings.TrimSpace(record.Fields["recovery_of"])
	if key == "" {
		key = faultCode
	}
	if isFault && key == "" {
		key = eventType + ":" + subsystem
	}

	evaluation := EventEvaluation{
		EventType:      eventType,
		Severity:       severity,
		SeverityRank:   rank,
		Lifecycle:      lifecycle,
		Subsystem:      subsystem,
		FaultCode:      faultCode,
		IsFault:        isFault,
		ActiveFaultKey: key,
	}

	e.mu.Lock()
	defer e.mu.Unlock()
	if key != "" {
		switch lifecycle {
		case EventActive, EventAcknowledged:
			if isFault {
				e.activeFaults[key] = evaluation
			}
		case EventCleared:
			// Recovery events may not themselves be classified as faults
			// (their event_class is RECOVERY and fault_code can be empty),
			// but they must still clear the referenced active fault.
			delete(e.activeFaults, key)
		}
	}
	return evaluation, nil
}

func (e *EventEngine) ActiveFaults() []EventEvaluation {
	e.mu.RLock()
	defer e.mu.RUnlock()
	values := make([]EventEvaluation, 0, len(e.activeFaults))
	for _, value := range e.activeFaults {
		values = append(values, value)
	}
	return values
}

func processEventEngineOutput(record GroundRecord, engine *EventEngine) (ProcessingResult, error) {
	if engine == nil {
		engine = defaultEventEngine
	}
	evaluation, err := engine.Evaluate(record)
	if err != nil {
		return ProcessingResult{}, err
	}
	operation := fmt.Sprintf("event-evaluate:%s:%s:%s", evaluation.Severity, evaluation.EventType, evaluation.Lifecycle)
	attributes := map[string]string{
		"event_type":         evaluation.EventType,
		"severity":           string(evaluation.Severity),
		"severity_rank":      fmt.Sprintf("%d", evaluation.SeverityRank),
		"lifecycle":          string(evaluation.Lifecycle),
		"is_fault":           fmt.Sprintf("%t", evaluation.IsFault),
		"active_fault_count": fmt.Sprintf("%d", len(engine.ActiveFaults())),
	}
	if evaluation.Subsystem != "" {
		attributes["subsystem"] = evaluation.Subsystem
	}
	if evaluation.FaultCode != "" {
		attributes["fault_code"] = evaluation.FaultCode
	}
	if evaluation.ActiveFaultKey != "" {
		attributes["active_fault_key"] = evaluation.ActiveFaultKey
	}
	if recoveryOf := strings.TrimSpace(record.Fields["recovery_of"]); recoveryOf != "" {
		attributes["recovery_of"] = recoveryOf
	}
	return buildProcessingResult(record, operation, attributes), nil
}
