package main

import (
	"testing"
	"time"
)

func TestBuildMissionCommandDetailViewUsesLifecycleAndExecution(t *testing.T) {
	ts := uint64(time.Now().UnixNano())
	record := GroundRecord{Envelope: GroundEnvelope{MissionID: "TRISHULA", CorrelationID: "CMD-DETAIL-001", MissionTimestamp: ts, SequenceNumber: 10}, Fields: map[string]string{"command": "START_ROVER", "target": "ROVER-01"}}
	lifecycle := &CommandLifecycleEvaluation{CommandID: "CMD-DETAIL-001", Command: "START_ROVER", Target: "ROVER-01", Lifecycle: CommandExecuting, Previous: CommandAcknowledged, Transition: "ACKNOWLEDGED->EXECUTING", Terminal: false, Sequence: 12}
	execution := &CommandExecutionReconciliation{CommandID: "CMD-DETAIL-001", Command: "START_ROVER", Target: "ROVER-01", State: CommandExecutionState("EXECUTING"), Terminal: false}

	view := buildMissionCommandDetailView("TRISHULA", "CMD-DETAIL-001", []GroundRecord{record}, lifecycle, execution, nil)
	if view.APIVersion != currentMissionCommandDetailViewVersion {
		t.Fatalf("unexpected api version: %s", view.APIVersion)
	}
	if view.Command != "START_ROVER" || view.Target != "ROVER-01" {
		t.Fatalf("unexpected command identity: %+v", view)
	}
	if view.RecordCount != 1 || view.Terminal {
		t.Fatalf("unexpected detail summary: %+v", view)
	}
}

func TestBuildMissionCommandDetailViewTerminalFromExecution(t *testing.T) {
	record := GroundRecord{Envelope: GroundEnvelope{MissionID: "TRISHULA", CorrelationID: "CMD-DETAIL-002"}, Fields: map[string]string{"command": "STOP_ROVER", "target": "ROVER-01"}}
	execution := &CommandExecutionReconciliation{CommandID: "CMD-DETAIL-002", Command: "STOP_ROVER", Target: "ROVER-01", State: CommandExecutionState("COMPLETED"), Terminal: true}
	view := buildMissionCommandDetailView("TRISHULA", "CMD-DETAIL-002", []GroundRecord{record}, nil, execution, nil)
	if !view.Terminal {
		t.Fatal("expected terminal execution")
	}
	if view.Command != "STOP_ROVER" || view.Target != "ROVER-01" {
		t.Fatalf("unexpected identity: %+v", view)
	}
}

func TestBuildMissionCommandDetailViewTerminalFromDurableRecords(t *testing.T) {
	records := []GroundRecord{
		{Envelope: GroundEnvelope{MissionID: "TRISHULA", CorrelationID: "CMD-DETAIL-003", SequenceNumber: 20}, Fields: map[string]string{"event_type": "COMMAND_COMPLETED", "execution_state": "COMPLETED"}},
	}
	view := buildMissionCommandDetailView("TRISHULA", "CMD-DETAIL-003", records, nil, nil, nil)
	if !view.Terminal {
		t.Fatal("expected terminal state from durable execution record")
	}
}
