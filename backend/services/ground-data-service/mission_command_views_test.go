package main

import "testing"

func TestBuildMissionCommandViewsUsesLatestRecord(t *testing.T) {
	records := []GroundRecord{
		{Envelope: GroundEnvelope{Kind: KindCommand, MissionID: "TRISHULA", CorrelationID: "CMD-1", RecordID: "CMD-1-S", MissionTimestamp: 20, SequenceNumber: 2}, Fields: map[string]string{"command": "START_ROVER", "target": "ROVER-01", "lifecycle": "SENT"}},
		{Envelope: GroundEnvelope{Kind: KindCommand, MissionID: "TRISHULA", CorrelationID: "CMD-1", RecordID: "CMD-1-Q", MissionTimestamp: 10, SequenceNumber: 1}, Fields: map[string]string{"command": "START_ROVER", "target": "ROVER-01", "lifecycle": "QUEUED"}},
	}
	view := buildMissionCommandViews("TRISHULA", records, nil, nil, 50)
	if view.Count != 1 || len(view.Commands) != 1 {
		t.Fatalf("unexpected count: %+v", view)
	}
	if view.Commands[0].LatestRecord.Envelope.RecordID != "CMD-1-S" || view.Commands[0].Lifecycle != "SENT" {
		t.Fatalf("unexpected view: %+v", view.Commands[0])
	}
}

func TestBuildMissionCommandViewsUsesExecutionState(t *testing.T) {
	records := []GroundRecord{{Envelope: GroundEnvelope{Kind: KindCommand, MissionID: "TRISHULA", CorrelationID: "CMD-2", RecordID: "CMD-2", MissionTimestamp: 20, SequenceNumber: 1}, Fields: map[string]string{"command": "STOP_ROVER", "target": "ROVER-01", "lifecycle": "SENT"}}}
	executions := []CommandExecutionReconciliation{{CommandID: "CMD-2", Command: "STOP_ROVER", Target: "ROVER-01", State: CommandCompletedExec, Terminal: true}}
	view := buildMissionCommandViews("TRISHULA", records, executions, nil, 50)
	if view.Commands[0].Lifecycle != "COMPLETED" || !view.Commands[0].Terminal || view.Commands[0].Execution == nil {
		t.Fatalf("unexpected execution view: %+v", view.Commands[0])
	}
}
