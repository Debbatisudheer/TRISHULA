package main

import (
	"testing"
)

func TestMissionCommandDispatcherBuildsCommandRecord(t *testing.T) {
	svc := NewService(100)
	dispatcher := NewMissionCommandDispatcher(svc, "GROUND-OPS-01")

	result, err := dispatcher.Submit("TRISHULA", CommandDispatchRequest{
		CommandID:      "CMD-DISPATCH-001",
		Command:        "STOP_ROVER",
		Target:         "ROVER-01",
		Priority:       "high",
		Reason:         "operator dispatch test",
		SequenceNumber: 1,
	})
	if err != nil {
		t.Fatal(err)
	}
	if !result.Accepted || result.Lifecycle != CommandReceived {
		t.Fatalf("unexpected dispatch result: %+v", result)
	}
	if result.Record.Envelope.Kind != KindCommand {
		t.Fatalf("unexpected kind: %+v", result.Record)
	}
	if result.Record.Envelope.DestinationNode != "ROVER-01" {
		t.Fatalf("unexpected destination: %+v", result.Record.Envelope)
	}
	if result.Record.Fields["lifecycle"] != string(CommandReceived) {
		t.Fatalf("unexpected lifecycle: %+v", result.Record.Fields)
	}
	if result.Record.Fields["command_id"] != "CMD-DISPATCH-001" {
		t.Fatalf("command id mismatch: %+v", result.Record.Fields)
	}
	if result.Record.Fields["reason"] != "operator dispatch test" {
		t.Fatalf("reason mismatch: %+v", result.Record.Fields)
	}
}

func TestMissionCommandDispatcherRejectsInvalidRequests(t *testing.T) {
	svc := NewService(100)
	dispatcher := NewMissionCommandDispatcher(svc, "GROUND-OPS-01")
	cases := []struct {
		name string
		req  CommandDispatchRequest
	}{
		{name: "missing command id", req: CommandDispatchRequest{Command: "STOP_ROVER", Target: "ROVER-01", SequenceNumber: 1}},
		{name: "missing command", req: CommandDispatchRequest{CommandID: "CMD-1", Target: "ROVER-01", SequenceNumber: 1}},
		{name: "missing target", req: CommandDispatchRequest{CommandID: "CMD-1", Command: "STOP_ROVER", SequenceNumber: 1}},
		{name: "missing sequence", req: CommandDispatchRequest{CommandID: "CMD-1", Command: "STOP_ROVER", Target: "ROVER-01"}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if _, err := dispatcher.Submit("TRISHULA", tc.req); err == nil {
				t.Fatal("expected validation error")
			}
		})
	}
}

func TestMissionCommandDispatcherPreservesDuplicateProtection(t *testing.T) {
	svc := NewService(100)
	dispatcher := NewMissionCommandDispatcher(svc, "GROUND-OPS-01")
	req := CommandDispatchRequest{CommandID: "CMD-DUP-001", Command: "STOP_ROVER", Target: "ROVER-01", SequenceNumber: 1}

	first, err := dispatcher.Submit("TRISHULA", req)
	if err != nil || !first.Accepted {
		t.Fatalf("first submission failed: %+v err=%v", first, err)
	}
	second, err := dispatcher.Submit("TRISHULA", req)
	if err != nil {
		t.Fatal(err)
	}
	if second.Accepted || !second.Ingest.Duplicate {
		t.Fatalf("expected duplicate rejection: %+v", second)
	}
}
