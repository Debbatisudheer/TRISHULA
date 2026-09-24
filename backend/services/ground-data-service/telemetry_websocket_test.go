package main

import (
	"encoding/json"
	"testing"
)

func TestBuildTelemetryWebSocketEnvelope(t *testing.T) {
	gateway := NewMissionControlWebSocketGateway()
	if gateway == nil {
		t.Fatal("gateway is nil")
	}
	record := GroundRecord{
		Envelope: GroundEnvelope{
			Kind: KindTelemetry, MissionID: "TRISHULA", SourceNode: "ROVER-01",
			RecordID: "TEL-099-001", SequenceNumber: 42, MissionTimestamp: 123456,
			CorrelationID: "TEL-099-001",
		},
		Fields: map[string]string{"metric": "battery", "value": "86.5"},
	}
	payload, err := json.Marshal(missionControlTelemetryEnvelope{
		Type: "mission_control.telemetry", StreamVersion: currentTelemetryStreamVersion,
		ProtocolVersion: "trishula-ws-v1", MissionID: record.Envelope.MissionID,
		SourceNode: record.Envelope.SourceNode, RecordID: record.Envelope.RecordID,
		SequenceNumber: record.Envelope.SequenceNumber, MissionTimestamp: record.Envelope.MissionTimestamp,
		CorrelationID: record.Envelope.CorrelationID, Fields: record.Fields,
	})
	if err != nil {
		t.Fatal(err)
	}
	var got missionControlTelemetryEnvelope
	if err := json.Unmarshal(payload, &got); err != nil {
		t.Fatal(err)
	}
	if got.Type != "mission_control.telemetry" || got.StreamVersion != currentTelemetryStreamVersion || got.ProtocolVersion != "trishula-ws-v1" {
		t.Fatalf("envelope = %+v", got)
	}
	if got.MissionID != "TRISHULA" || got.SourceNode != "ROVER-01" || got.SequenceNumber != 42 || got.Fields["value"] != "86.5" {
		t.Fatalf("telemetry projection = %+v", got)
	}
}

func TestPublishTelemetryIgnoresNonTelemetry(t *testing.T) {
	gateway := NewMissionControlWebSocketGateway()
	gateway.PublishTelemetry(GroundRecord{Envelope: GroundEnvelope{Kind: KindEvent}})
	if gateway.ClientCount() != 0 {
		t.Fatalf("clients = %d", gateway.ClientCount())
	}
}
