package main

import (
	"context"
	"testing"
)

type alertDetailTestRepository struct{ records []GroundRecord }

func (r *alertDetailTestRepository) Append(record GroundRecord) error {
	r.records = append(r.records, record)
	return nil
}
func (r *alertDetailTestRepository) Close() error { return nil }
func (r *alertDetailTestRepository) GetByID(ctx context.Context, id string) (GroundRecord, bool, error) {
	for _, x := range r.records {
		if x.Envelope.RecordID == id {
			return x, true, nil
		}
	}
	return GroundRecord{}, false, nil
}
func (r *alertDetailTestRepository) Query(ctx context.Context, q RecordQuery) ([]GroundRecord, error) {
	return applyQuery(r.records, q)
}

func TestMissionControlAlertDetailBuildsAudit(t *testing.T) {
	manager := NewMissionControlAlertManager()
	active := alertEventRecord("V103-ACTIVE", "ENGINE_FAULT", "ERROR", "ACTIVE", "ENGINE_FAULT")
	alert, ok := manager.ProcessEvent(active)
	if !ok {
		t.Fatal("expected alert")
	}
	ack := manager.LifecycleRecord(alert, "ACKNOWLEDGE", "operator-1", "reviewed", 900001)
	repo := &alertDetailTestRepository{records: []GroundRecord{active, ack}}
	detail, err := buildMissionControlAlertDetail(context.Background(), repo, alert, "req-103")
	if err != nil {
		t.Fatal(err)
	}
	if detail.APIVersion != "v0.9.103" || detail.Alert.AlertID != alert.AlertID {
		t.Fatalf("detail=%+v", detail)
	}
	if detail.AuditCount != 1 || detail.Audit[0].EventType != "ALERT_ACKNOWLEDGE" || detail.Audit[0].OperatorID != "operator-1" {
		t.Fatalf("audit=%+v", detail.Audit)
	}
}

func TestMissionControlAlertHistoryIsChronological(t *testing.T) {
	manager := NewMissionControlAlertManager()
	active := alertEventRecord("V103-ACTIVE-2", "ENGINE_FAULT", "ERROR", "ACTIVE", "ENGINE_FAULT")
	alert, ok := manager.ProcessEvent(active)
	if !ok {
		t.Fatal("expected alert")
	}
	ack := manager.LifecycleRecord(alert, "ACKNOWLEDGE", "operator-1", "reviewed", 900002)
	clear := manager.LifecycleRecord(alert, "CLEAR", "operator-1", "resolved", 900003)
	repo := &alertDetailTestRepository{records: []GroundRecord{clear, ack, active}}
	history, err := buildMissionControlAlertHistory(context.Background(), repo, alert, "req-103")
	if err != nil {
		t.Fatal(err)
	}
	if history.Count != 2 || history.Entries[0].EventType != "ALERT_ACKNOWLEDGE" || history.Entries[1].EventType != "ALERT_CLEAR" {
		t.Fatalf("history=%+v", history)
	}
}
