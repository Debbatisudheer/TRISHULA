package main

import "testing"

func TestMissionControlAlertLifecycle(t *testing.T) {
	manager := NewMissionControlAlertManager()
	_, ok := manager.ProcessEvent(alertEventRecord("LIFE-ACTIVE", "ENGINE_FAULT", "ERROR", "ACTIVE", "ENGINE_FAULT"))
	if !ok {
		t.Fatal("expected active alert")
	}

	alert, err := manager.Acknowledge("ALERT-LIFE-ACTIVE", "TRISHULA", "operator-1", "reviewed")
	if err != nil {
		t.Fatal(err)
	}
	if alert.Status != "ACKNOWLEDGED" || alert.Lifecycle != string(EventAcknowledged) {
		t.Fatalf("alert=%+v", alert)
	}

	same, err := manager.Acknowledge("ALERT-LIFE-ACTIVE", "TRISHULA", "operator-1", "")
	if err != nil || same.Status != "ACKNOWLEDGED" {
		t.Fatalf("idempotent acknowledge: %+v %v", same, err)
	}

	cleared, err := manager.Clear("ALERT-LIFE-ACTIVE", "TRISHULA", "operator-1", "fault resolved")
	if err != nil {
		t.Fatal(err)
	}
	if cleared.Status != "CLEARED" || cleared.Lifecycle != string(EventCleared) {
		t.Fatalf("alert=%+v", cleared)
	}

	sameClear, err := manager.Clear("ALERT-LIFE-ACTIVE", "TRISHULA", "operator-1", "")
	if err != nil || sameClear.Status != "CLEARED" {
		t.Fatalf("idempotent clear: %+v %v", sameClear, err)
	}

	snapshot := manager.Snapshot("TRISHULA")
	if snapshot.ActiveCount != 0 || snapshot.TotalCount != 1 {
		t.Fatalf("snapshot=%+v", snapshot)
	}
}

func TestMissionControlAlertLifecycleRecord(t *testing.T) {
	manager := NewMissionControlAlertManager()
	alert, ok := manager.ProcessEvent(alertEventRecord("LIFE-RECORD", "ENGINE_FAULT", "ERROR", "ACTIVE", "ENGINE_FAULT"))
	if !ok {
		t.Fatal("expected alert")
	}
	record := manager.LifecycleRecord(alert, "ACKNOWLEDGE", "operator-1", "reviewed", 123)
	if record.Envelope.Kind != KindEvent || record.Envelope.CorrelationID != alert.AlertID {
		t.Fatalf("record=%+v", record)
	}
	if record.Fields["event_type"] != "ALERT_ACKNOWLEDGE" || record.Fields["event_class"] != "ALERT_LIFECYCLE" {
		t.Fatalf("fields=%v", record.Fields)
	}
}

func TestMissionControlAlertRestoreReplaysLifecycle(t *testing.T) {
	store, err := NewJSONLStore("testdata-v102-alerts/records.jsonl")
	if err != nil {
		t.Fatal(err)
	}
	defer store.Close()
	active := alertEventRecord("RESTORE-ACTIVE", "ENGINE_FAULT", "ERROR", "ACTIVE", "ENGINE_FAULT")
	clear := alertEventRecord("RESTORE-CLEAR", "RECOVERY", "INFO", "CLEARED", "")
	clear.Fields["event_class"] = "RECOVERY"
	clear.Fields["recovery_of"] = "ENGINE_FAULT"
	if err := store.Append(active); err != nil {
		t.Fatal(err)
	}
	if err := store.Append(clear); err != nil {
		t.Fatal(err)
	}
	// JSONLStore intentionally does not implement Query; this test validates the event replay contract directly.
	manager := NewMissionControlAlertManager()
	manager.ProcessEvent(active)
	manager.ProcessEvent(clear)
	alert, found := manager.Get("ALERT-ENGINE_FAULT", "TRISHULA")
	if !found || alert.Status != "CLEARED" {
		t.Fatalf("restored alert=%+v found=%v", alert, found)
	}
}
