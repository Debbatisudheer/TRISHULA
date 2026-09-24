package main

import (
	"context"
	"fmt"
	"net/http"
	"sort"
	"strings"
	"time"
)

const currentMissionControlAlertDetailVersion = "v0.9.103"

type MissionControlAlertAuditEntry struct {
	RecordID         string    `json:"record_id"`
	AlertID          string    `json:"alert_id"`
	MissionID        string    `json:"mission_id"`
	EventType        string    `json:"event_type"`
	EventClass       string    `json:"event_class"`
	Lifecycle        string    `json:"lifecycle"`
	Status           string    `json:"status"`
	OperatorID       string    `json:"operator_id,omitempty"`
	Reason           string    `json:"reason,omitempty"`
	SequenceNumber   uint64    `json:"sequence_number"`
	MissionTimestamp uint64    `json:"mission_timestamp_ns"`
	CorrelationID    string    `json:"correlation_id,omitempty"`
	RecordedAt       time.Time `json:"recorded_at"`
}

type MissionControlAlertDetail struct {
	RequestID   string                          `json:"request_id"`
	APIVersion  string                          `json:"api_version"`
	Alert       MissionControlAlert             `json:"alert"`
	Audit       []MissionControlAlertAuditEntry `json:"audit"`
	AuditCount  int                             `json:"audit_count"`
	GeneratedAt time.Time                       `json:"generated_at"`
}

type MissionControlAlertHistory struct {
	RequestID   string                          `json:"request_id"`
	APIVersion  string                          `json:"api_version"`
	AlertID     string                          `json:"alert_id"`
	MissionID   string                          `json:"mission_id"`
	Count       int                             `json:"count"`
	Entries     []MissionControlAlertAuditEntry `json:"entries"`
	GeneratedAt time.Time                       `json:"generated_at"`
}

type AlertAuditRepository interface {
	Query(ctx context.Context, query RecordQuery) ([]GroundRecord, error)
}

func buildMissionControlAlertAudit(ctx context.Context, repository AlertAuditRepository, alert MissionControlAlert) ([]MissionControlAlertAuditEntry, error) {
	if repository == nil {
		return []MissionControlAlertAuditEntry{}, nil
	}
	records, err := repository.Query(ctx, RecordQuery{MissionID: alert.MissionID, Kind: KindEvent, Limit: 100000})
	if err != nil {
		return nil, err
	}
	entries := make([]MissionControlAlertAuditEntry, 0)
	for _, record := range records {
		fields := record.Fields
		if strings.TrimSpace(fields["alert_id"]) != alert.AlertID {
			continue
		}
		eventType := strings.TrimSpace(fields["event_type"])
		eventClass := strings.TrimSpace(fields["event_class"])
		if eventClass != "ALERT_LIFECYCLE" && !strings.HasPrefix(strings.ToUpper(eventType), "ALERT_") {
			continue
		}
		recordedAt := time.Unix(0, int64(record.Envelope.MissionTimestamp)).UTC()
		if record.Envelope.MissionTimestamp == 0 {
			recordedAt = time.Time{}
		}
		entries = append(entries, MissionControlAlertAuditEntry{
			RecordID: record.Envelope.RecordID, AlertID: alert.AlertID, MissionID: alert.MissionID,
			EventType: eventType, EventClass: eventClass, Lifecycle: strings.TrimSpace(fields["lifecycle"]),
			Status: strings.TrimSpace(fields["lifecycle"]), OperatorID: strings.TrimSpace(fields["operator_id"]),
			Reason: strings.TrimSpace(fields["message"]), SequenceNumber: record.Envelope.SequenceNumber,
			MissionTimestamp: record.Envelope.MissionTimestamp, CorrelationID: record.Envelope.CorrelationID,
			RecordedAt: recordedAt,
		})
	}
	sort.Slice(entries, func(i, j int) bool {
		if entries[i].MissionTimestamp != entries[j].MissionTimestamp {
			return entries[i].MissionTimestamp < entries[j].MissionTimestamp
		}
		if entries[i].SequenceNumber != entries[j].SequenceNumber {
			return entries[i].SequenceNumber < entries[j].SequenceNumber
		}
		return entries[i].RecordID < entries[j].RecordID
	})
	return entries, nil
}

func buildMissionControlAlertDetail(ctx context.Context, repository AlertAuditRepository, alert MissionControlAlert, requestID string) (MissionControlAlertDetail, error) {
	audit, err := buildMissionControlAlertAudit(ctx, repository, alert)
	if err != nil {
		return MissionControlAlertDetail{}, err
	}
	return MissionControlAlertDetail{RequestID: requestID, APIVersion: currentMissionControlAlertDetailVersion, Alert: alert, Audit: audit, AuditCount: len(audit), GeneratedAt: time.Now().UTC()}, nil
}

func buildMissionControlAlertHistory(ctx context.Context, repository AlertAuditRepository, alert MissionControlAlert, requestID string) (MissionControlAlertHistory, error) {
	audit, err := buildMissionControlAlertAudit(ctx, repository, alert)
	if err != nil {
		return MissionControlAlertHistory{}, err
	}
	return MissionControlAlertHistory{RequestID: requestID, APIVersion: currentMissionControlAlertDetailVersion, AlertID: alert.AlertID, MissionID: alert.MissionID, Count: len(audit), Entries: audit, GeneratedAt: time.Now().UTC()}, nil
}

func validateAlertDetailRepository(repository RecordRepository) error {
	if repository == nil {
		return fmt.Errorf("ground record repository unavailable")
	}
	return nil
}

func handleMissionControlAlertDetail(w http.ResponseWriter, r *http.Request, consumer *KafkaConsumer, repository RecordRepository) {
	if consumer == nil {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "kafka consumer unavailable"})
		return
	}
	if err := validateAlertDetailRepository(repository); err != nil {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": err.Error()})
		return
	}
	alertID := strings.TrimSpace(r.PathValue("alertID"))
	missionID := strings.TrimSpace(r.URL.Query().Get("mission_id"))
	if alertID == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "alertID is required"})
		return
	}
	alert, ok := consumer.Alert(alertID, missionID)
	if !ok {
		writeJSON(w, http.StatusNotFound, map[string]string{"error": "alert not found"})
		return
	}
	detail, err := buildMissionControlAlertDetail(r.Context(), repository, alert, requestID(r))
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "failed to build alert detail: " + err.Error()})
		return
	}
	w.Header().Set("X-TRISHULA-API-VERSION", currentMissionControlAlertDetailVersion)
	writeJSON(w, http.StatusOK, detail)
}

func handleMissionControlAlertHistory(w http.ResponseWriter, r *http.Request, consumer *KafkaConsumer, repository RecordRepository) {
	if consumer == nil {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "kafka consumer unavailable"})
		return
	}
	if err := validateAlertDetailRepository(repository); err != nil {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": err.Error()})
		return
	}
	alertID := strings.TrimSpace(r.PathValue("alertID"))
	missionID := strings.TrimSpace(r.URL.Query().Get("mission_id"))
	if alertID == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "alertID is required"})
		return
	}
	alert, ok := consumer.Alert(alertID, missionID)
	if !ok {
		writeJSON(w, http.StatusNotFound, map[string]string{"error": "alert not found"})
		return
	}
	history, err := buildMissionControlAlertHistory(r.Context(), repository, alert, requestID(r))
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "failed to build alert history: " + err.Error()})
		return
	}
	w.Header().Set("X-TRISHULA-API-VERSION", currentMissionControlAlertDetailVersion)
	writeJSON(w, http.StatusOK, history)
}
