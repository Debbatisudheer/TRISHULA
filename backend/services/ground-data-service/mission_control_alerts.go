package main

import (
	"context"
	"fmt"
	"sort"
	"strings"
	"sync"
	"time"
)

const currentMissionControlAlertVersion = "v0.9.103"

type MissionControlAlert struct {
	AlertID          string    `json:"alert_id"`
	MissionID        string    `json:"mission_id"`
	SourceNode       string    `json:"source_node"`
	RecordID         string    `json:"record_id"`
	SequenceNumber   uint64    `json:"sequence_number"`
	MissionTimestamp uint64    `json:"mission_timestamp_ns"`
	EventType        string    `json:"event_type"`
	Severity         string    `json:"severity"`
	Lifecycle        string    `json:"lifecycle"`
	Status           string    `json:"status"`
	Subsystem        string    `json:"subsystem,omitempty"`
	FaultCode        string    `json:"fault_code,omitempty"`
	AlertKey         string    `json:"alert_key"`
	Message          string    `json:"message"`
	CorrelationID    string    `json:"correlation_id,omitempty"`
	CreatedAt        time.Time `json:"created_at"`
	UpdatedAt        time.Time `json:"updated_at"`
}

type MissionControlAlertSnapshot struct {
	Version     string                `json:"version"`
	ActiveCount int                   `json:"active_count"`
	TotalCount  int                   `json:"total_count"`
	Alerts      []MissionControlAlert `json:"alerts"`
	GeneratedAt time.Time             `json:"generated_at"`
}

type MissionControlAlertManager struct {
	mu     sync.RWMutex
	alerts map[string]MissionControlAlert
}

func NewMissionControlAlertManager() *MissionControlAlertManager {
	return &MissionControlAlertManager{alerts: make(map[string]MissionControlAlert)}
}

func (m *MissionControlAlertManager) ProcessEvent(record GroundRecord) (MissionControlAlert, bool) {
	if m == nil || record.Envelope.Kind != KindEvent {
		return MissionControlAlert{}, false
	}
	eventType := strings.TrimSpace(record.Fields["event_type"])
	if eventType == "" {
		return MissionControlAlert{}, false
	}

	// Lifecycle audit records refer to an existing alert by alert_id. Replay
	// them as state transitions instead of creating a second alert entry.
	if strings.EqualFold(strings.TrimSpace(record.Fields["event_class"]), "ALERT_LIFECYCLE") {
		alertID := strings.TrimSpace(record.Fields["alert_id"])
		if alertID == "" {
			return MissionControlAlert{}, false
		}
		m.mu.Lock()
		defer m.mu.Unlock()
		for key, alert := range m.alerts {
			if alert.AlertID != alertID || alert.MissionID != record.Envelope.MissionID {
				continue
			}
			status := strings.ToUpper(strings.TrimSpace(record.Fields["lifecycle"]))
			if status == "" {
				status = strings.TrimPrefix(strings.ToUpper(eventType), "ALERT_")
			}
			switch status {
			case string(EventAcknowledged):
				alert.Status = "ACKNOWLEDGED"
				alert.Lifecycle = string(EventAcknowledged)
			case string(EventCleared):
				alert.Status = "CLEARED"
				alert.Lifecycle = string(EventCleared)
			default:
				return MissionControlAlert{}, false
			}
			alert.UpdatedAt = time.Now().UTC()
			if reason := strings.TrimSpace(record.Fields["message"]); reason != "" {
				alert.Message = reason
			}
			m.alerts[key] = alert
			return alert, true
		}
		return MissionControlAlert{}, false
	}
	severity, rank, err := normalizeEventSeverity(record.Fields["severity"])
	if err != nil {
		return MissionControlAlert{}, false
	}
	eventClass := strings.ToUpper(strings.TrimSpace(record.Fields["event_class"]))
	faultCode := strings.TrimSpace(record.Fields["fault_code"])
	isRecovery := strings.EqualFold(eventType, "RECOVERY") || eventClass == "RECOVERY" || strings.TrimSpace(record.Fields["recovery_of"]) != ""
	if !isRecovery && rank < 2 {
		return MissionControlAlert{}, false
	}
	key := strings.TrimSpace(record.Fields["recovery_of"])
	if key == "" {
		key = faultCode
	}
	if key == "" {
		key = eventType + ":" + strings.TrimSpace(record.Fields["subsystem"])
	}
	if key == "" {
		key = record.Envelope.RecordID
	}
	status := "ACTIVE"
	lifecycle := strings.ToUpper(strings.TrimSpace(record.Fields["lifecycle"]))
	if isRecovery || lifecycle == string(EventCleared) {
		status = "CLEARED"
	} else if lifecycle == string(EventAcknowledged) {
		status = "ACKNOWLEDGED"
	}
	now := time.Now().UTC()
	alertID := "ALERT-" + record.Envelope.RecordID
	if status == "CLEARED" {
		alertID = "ALERT-" + key
	}
	alert := MissionControlAlert{
		AlertID: alertID, MissionID: record.Envelope.MissionID, SourceNode: record.Envelope.SourceNode,
		RecordID: record.Envelope.RecordID, SequenceNumber: record.Envelope.SequenceNumber,
		MissionTimestamp: record.Envelope.MissionTimestamp, EventType: eventType, Severity: string(severity),
		Lifecycle: lifecycleOrDefault(lifecycle, status), Status: status, Subsystem: strings.TrimSpace(record.Fields["subsystem"]),
		FaultCode: faultCode, AlertKey: key, Message: alertMessage(eventType, string(severity), record.Fields),
		CorrelationID: record.Envelope.CorrelationID, CreatedAt: now, UpdatedAt: now,
	}
	m.mu.Lock()
	if existing, ok := m.alerts[key]; ok {
		alert.CreatedAt = existing.CreatedAt
	}
	m.alerts[key] = alert
	m.mu.Unlock()
	return alert, true
}

func lifecycleOrDefault(value, status string) string {
	if value != "" {
		return value
	}
	return status
}

func alertMessage(eventType, severity string, fields map[string]string) string {
	if message := strings.TrimSpace(fields["message"]); message != "" {
		return message
	}
	subsystem := strings.TrimSpace(fields["subsystem"])
	if subsystem != "" {
		return fmt.Sprintf("%s %s alert in %s", severity, eventType, subsystem)
	}
	return fmt.Sprintf("%s %s alert", severity, eventType)
}

func (m *MissionControlAlertManager) Snapshot(missionID string) MissionControlAlertSnapshot {
	now := time.Now().UTC()
	m.mu.RLock()
	alerts := make([]MissionControlAlert, 0, len(m.alerts))
	for _, alert := range m.alerts {
		if missionID == "" || alert.MissionID == missionID {
			alerts = append(alerts, alert)
		}
	}
	m.mu.RUnlock()
	sort.Slice(alerts, func(i, j int) bool {
		if alerts[i].UpdatedAt.Equal(alerts[j].UpdatedAt) {
			return alerts[i].AlertID < alerts[j].AlertID
		}
		return alerts[i].UpdatedAt.After(alerts[j].UpdatedAt)
	})
	active := 0
	for _, alert := range alerts {
		if alert.Status == "ACTIVE" || alert.Status == "ACKNOWLEDGED" {
			active++
		}
	}
	return MissionControlAlertSnapshot{Version: currentMissionControlAlertVersion, ActiveCount: active, TotalCount: len(alerts), Alerts: alerts, GeneratedAt: now}
}

type MissionControlAlertLifecycleAction struct {
	OperatorID string `json:"operator_id"`
	Reason     string `json:"reason,omitempty"`
}

func (m *MissionControlAlertManager) Get(alertID, missionID string) (MissionControlAlert, bool) {
	if m == nil {
		return MissionControlAlert{}, false
	}
	m.mu.RLock()
	defer m.mu.RUnlock()
	for _, alert := range m.alerts {
		if alert.AlertID == alertID && (missionID == "" || alert.MissionID == missionID) {
			return alert, true
		}
	}
	return MissionControlAlert{}, false
}

func (m *MissionControlAlertManager) Acknowledge(alertID, missionID, operatorID, reason string) (MissionControlAlert, error) {
	if m == nil {
		return MissionControlAlert{}, fmt.Errorf("alert manager unavailable")
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	key, alert, ok := m.findByIDLocked(alertID, missionID)
	if !ok {
		return MissionControlAlert{}, fmt.Errorf("alert not found")
	}
	if alert.Status == "CLEARED" {
		return alert, fmt.Errorf("cleared alert cannot be acknowledged")
	}
	if alert.Status == "ACKNOWLEDGED" {
		return alert, nil
	}
	alert.Status = "ACKNOWLEDGED"
	alert.Lifecycle = string(EventAcknowledged)
	alert.UpdatedAt = time.Now().UTC()
	if reason != "" {
		alert.Message = fmt.Sprintf("%s (acknowledged by %s: %s)", alert.Message, operatorID, reason)
	}
	m.alerts[key] = alert
	return alert, nil
}

func (m *MissionControlAlertManager) Clear(alertID, missionID, operatorID, reason string) (MissionControlAlert, error) {
	if m == nil {
		return MissionControlAlert{}, fmt.Errorf("alert manager unavailable")
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	key, alert, ok := m.findByIDLocked(alertID, missionID)
	if !ok {
		return MissionControlAlert{}, fmt.Errorf("alert not found")
	}
	if alert.Status == "CLEARED" {
		return alert, nil
	}
	alert.Status = "CLEARED"
	alert.Lifecycle = string(EventCleared)
	alert.UpdatedAt = time.Now().UTC()
	if reason != "" {
		alert.Message = fmt.Sprintf("%s (cleared by %s: %s)", alert.Message, operatorID, reason)
	}
	m.alerts[key] = alert
	return alert, nil
}

func (m *MissionControlAlertManager) findByIDLocked(alertID, missionID string) (string, MissionControlAlert, bool) {
	for key, alert := range m.alerts {
		if alert.AlertID == alertID && (missionID == "" || alert.MissionID == missionID) {
			return key, alert, true
		}
	}
	return "", MissionControlAlert{}, false
}

func (m *MissionControlAlertManager) Restore(ctx context.Context, repository RecordRepository, missionID string) error {
	if m == nil || repository == nil {
		return nil
	}
	records, err := repository.Query(ctx, RecordQuery{MissionID: missionID, Kind: KindEvent, Limit: 100000})
	if err != nil {
		return err
	}
	// Query is newest-first; replay oldest-first so lifecycle transitions reconstruct deterministically.
	sort.SliceStable(records, func(i, j int) bool {
		a, b := records[i].Envelope, records[j].Envelope
		if a.MissionTimestamp != b.MissionTimestamp {
			return a.MissionTimestamp < b.MissionTimestamp
		}
		if a.SequenceNumber != b.SequenceNumber {
			return a.SequenceNumber < b.SequenceNumber
		}
		return a.RecordID < b.RecordID
	})
	for _, record := range records {
		m.ProcessEvent(record)
	}
	return nil
}

func (m *MissionControlAlertManager) LifecycleRecord(alert MissionControlAlert, action, operatorID, reason string, sequence uint64) GroundRecord {
	now := uint64(time.Now().UTC().UnixNano())
	fields := map[string]string{
		"event_type": "ALERT_" + strings.ToUpper(action), "severity": alert.Severity, "event_class": "ALERT_LIFECYCLE",
		"lifecycle": alert.Status, "fault_code": alert.FaultCode, "subsystem": alert.Subsystem,
		"alert_id": alert.AlertID, "alert_key": alert.AlertKey, "operator_id": operatorID, "message": reason,
	}
	return GroundRecord{Envelope: GroundEnvelope{SchemaVersion: 1, Kind: KindEvent, RecordID: fmt.Sprintf("%s-%s-%d", alert.AlertID, strings.ToLower(action), sequence), MissionID: alert.MissionID, SourceNode: "GROUND-OPS-01", OriginNode: "GROUND-OPS-01", DestinationNode: alert.SourceNode, Priority: "high", ApplicationID: 501, MissionTimestamp: now, SequenceNumber: sequence, Quality: 1, CorrelationID: alert.AlertID, PayloadSchema: "event.v1"}, Fields: fields}
}
