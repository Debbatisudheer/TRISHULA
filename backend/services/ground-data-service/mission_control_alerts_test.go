package main

import (
	"encoding/json"
	"testing"
)

func alertEventRecord(recordID, eventType, severity, lifecycle, faultCode string) GroundRecord {
	return GroundRecord{Envelope: GroundEnvelope{
		Kind: KindEvent, MissionID: "TRISHULA", SourceNode: "ROVER-01", RecordID: recordID,
		SequenceNumber: 700001, MissionTimestamp: 123456789, CorrelationID: recordID,
	}, Fields: map[string]string{
		"event_type": eventType, "severity": severity, "lifecycle": lifecycle,
		"event_class": "FAULT", "fault_code": faultCode, "subsystem": "thermal",
	}}
}

func TestMissionControlAlertManagerCreatesActiveAlert(t *testing.T) {
	manager := NewMissionControlAlertManager()
	alert, ok := manager.ProcessEvent(alertEventRecord("ALERT-EVENT-001", "THERMAL_OVER_TEMP", "CRITICAL", "ACTIVE", "THERMAL_OVER_TEMP"))
	if !ok {
		t.Fatal("expected alert")
	}
	if alert.Status != "ACTIVE" || alert.Severity != "CRITICAL" || alert.AlertKey != "THERMAL_OVER_TEMP" {
		t.Fatalf("alert = %+v", alert)
	}
	snapshot := manager.Snapshot("TRISHULA")
	if snapshot.ActiveCount != 1 || snapshot.TotalCount != 1 {
		t.Fatalf("snapshot = %+v", snapshot)
	}
}

func TestMissionControlAlertManagerClearsExistingAlert(t *testing.T) {
	manager := NewMissionControlAlertManager()
	_, ok := manager.ProcessEvent(alertEventRecord("ALERT-EVENT-002", "ENGINE_FAULT", "ERROR", "ACTIVE", "ENGINE_FAULT"))
	if !ok {
		t.Fatal("expected active alert")
	}
	recovery := alertEventRecord("ALERT-EVENT-003", "RECOVERY", "INFO", "CLEARED", "")
	recovery.Fields["event_class"] = "RECOVERY"
	recovery.Fields["recovery_of"] = "ENGINE_FAULT"
	alert, ok := manager.ProcessEvent(recovery)
	if !ok {
		t.Fatal("expected recovery alert")
	}
	if alert.Status != "CLEARED" || alert.AlertKey != "ENGINE_FAULT" {
		t.Fatalf("alert = %+v", alert)
	}
	snapshot := manager.Snapshot("TRISHULA")
	if snapshot.ActiveCount != 0 || snapshot.TotalCount != 1 {
		t.Fatalf("snapshot = %+v", snapshot)
	}
}

func TestMissionControlAlertManagerIgnoresInfo(t *testing.T) {
	manager := NewMissionControlAlertManager()
	if _, ok := manager.ProcessEvent(alertEventRecord("ALERT-EVENT-004", "INFO_EVENT", "INFO", "ACTIVE", "")); ok {
		t.Fatal("info event should not create alert")
	}
}

func TestBuildMissionControlAlertEnvelope(t *testing.T) {
	alert := MissionControlAlert{AlertID: "ALERT-001", MissionID: "TRISHULA", SourceNode: "ROVER-01", RecordID: "EVENT-001", SequenceNumber: 7, MissionTimestamp: 9, EventType: "THERMAL_OVER_TEMP", Severity: "CRITICAL", Lifecycle: "ACTIVE", Status: "ACTIVE", AlertKey: "THERMAL", Message: "CRITICAL THERMAL_OVER_TEMP alert in thermal"}
	payload, err := json.Marshal(missionControlAlertEnvelope{Type: "mission_control.alert", StreamVersion: currentAlertStreamVersion, ProtocolVersion: "trishula-ws-v1", MissionID: alert.MissionID, SourceNode: alert.SourceNode, RecordID: alert.RecordID, SequenceNumber: alert.SequenceNumber, MissionTimestamp: alert.MissionTimestamp, AlertID: alert.AlertID, EventType: alert.EventType, Severity: alert.Severity, Lifecycle: alert.Lifecycle, Status: alert.Status, AlertKey: alert.AlertKey, Message: alert.Message})
	if err != nil {
		t.Fatal(err)
	}
	var got missionControlAlertEnvelope
	if err := json.Unmarshal(payload, &got); err != nil {
		t.Fatal(err)
	}
	if got.Type != "mission_control.alert" || got.StreamVersion != "v0.9.102" || got.ProtocolVersion != "trishula-ws-v1" {
		t.Fatalf("envelope = %+v", got)
	}
	if got.AlertID != "ALERT-001" || got.Severity != "CRITICAL" || got.Status != "ACTIVE" {
		t.Fatalf("alert projection = %+v", got)
	}
}
