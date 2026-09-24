package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"testing"
)

func TestBuildCommandExecutionWebSocketEnvelope(t *testing.T) {
	gateway := NewMissionControlWebSocketGateway()
	left, right := netPipe(t)
	client := &missionControlWebSocketClient{conn: left}
	gateway.register(client)
	defer func() {
		gateway.unregister(client)
		_ = left.Close()
		_ = right.Close()
	}()

	record := GroundRecord{
		Envelope: GroundEnvelope{
			Kind: KindEvent, MissionID: "TRISHULA", SourceNode: "GROUND-OPS-01",
			RecordID: "CMD-100-001-command-executing", SequenceNumber: 700001,
			MissionTimestamp: 123456789, CorrelationID: "CMD-100-001",
		},
		Fields: map[string]string{
			"command_id": "CMD-100-001", "command": "DRIVE", "target": "ROVER-01",
			"event_class": "COMMAND", "event_type": "COMMAND_EXECUTING",
			"execution_state": "EXECUTING", "vehicle_response": "drive in progress",
			"severity": "INFO", "x_m": "12.5",
		},
	}

	go gateway.PublishCommandExecution(record)
	opcode, payload, err := readWebSocketFrameForTest(bufio.NewReader(right))
	if err != nil {
		t.Fatal(err)
	}
	if opcode != 0x1 {
		t.Fatalf("opcode = %d, want text", opcode)
	}
	var got missionControlCommandExecutionEnvelope
	if err := json.Unmarshal(payload, &got); err != nil {
		t.Fatal(err)
	}
	if got.Type != "mission_control.command_execution" || got.StreamVersion != currentCommandStreamVersion || got.ProtocolVersion != "trishula-ws-v1" {
		t.Fatalf("envelope = %+v", got)
	}
	if got.CommandID != "CMD-100-001" || got.ExecutionState != "EXECUTING" || got.EventType != "COMMAND_EXECUTING" || got.Target != "ROVER-01" {
		t.Fatalf("command projection = %+v", got)
	}
	if !bytes.Equal([]byte(got.Fields["x_m"]), []byte("12.5")) {
		t.Fatalf("fields = %+v", got.Fields)
	}
}

func TestPublishCommandExecutionIgnoresNonCommandEvent(t *testing.T) {
	gateway := NewMissionControlWebSocketGateway()
	left, right := netPipe(t)
	client := &missionControlWebSocketClient{conn: left}
	gateway.register(client)
	defer func() {
		gateway.unregister(client)
		_ = left.Close()
		_ = right.Close()
	}()

	gateway.PublishCommandExecution(GroundRecord{
		Envelope: GroundEnvelope{Kind: KindEvent, MissionID: "TRISHULA", RecordID: "EVENT-100-001"},
		Fields:   map[string]string{"event_type": "SCIENCE_UPDATE", "event_class": "SCIENCE", "command_id": "CMD-100-001"},
	})

	if gateway.ClientCount() != 1 {
		t.Fatalf("clients = %d, want 1", gateway.ClientCount())
	}
}
