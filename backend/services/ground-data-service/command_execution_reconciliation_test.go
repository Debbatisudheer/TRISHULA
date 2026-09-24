package main

import (
	"testing"
	"time"
)

func executionCommandRecord(id string, seq uint64, state string) GroundRecord {
	return GroundRecord{
		Envelope: GroundEnvelope{
			SchemaVersion:    1,
			Kind:             KindCommand,
			RecordID:         id,
			MissionID:        "TRISHULA",
			SourceNode:       "GS-TRISHULA-01",
			OriginNode:       "GS-TRISHULA-01",
			DestinationNode:  "ROVER-01",
			Priority:         "high",
			ApplicationID:    401,
			MissionTimestamp: 1000000 + seq,
			SequenceNumber:   seq,
			Quality:          1.0,
			CorrelationID:    id,
			PayloadSchema:    "command.v1",
		},
		Fields: map[string]string{
			"command_id": id,
			"command":    "STOP_ROVER",
			"target":     "ROVER-01",
			"lifecycle":  state,
		},
	}
}

func executionFeedbackRecord(id string, seq uint64, state string, eventType string) GroundRecord {
	return GroundRecord{
		Envelope: GroundEnvelope{
			SchemaVersion:    1,
			Kind:             KindEvent,
			RecordID:         id,
			MissionID:        "TRISHULA",
			SourceNode:       "ROVER-01",
			OriginNode:       "ROVER-01",
			DestinationNode:  "GS-TRISHULA-01",
			Priority:         "high",
			ApplicationID:    301,
			MissionTimestamp: 1000000 + seq,
			SequenceNumber:   seq,
			Quality:          1.0,
			PayloadSchema:    "event.v1",
		},
		Fields: map[string]string{
			"command_id":       "CMD-EXEC-1",
			"event_type":       eventType,
			"severity":         "INFO",
			"event_class":      "COMMAND",
			"execution_state":  state,
			"vehicle_response": "ROVER-01 acknowledged",
		},
	}
}

func TestCommandExecutionReconciliationHappyPath(t *testing.T) {
	r := NewCommandExecutionReconciler()
	states := []string{"VALIDATED", "QUEUED", "SENT", "ACKNOWLEDGED", "EXECUTING", "COMPLETED"}
	for i, state := range states {
		record := executionCommandRecord("CMD-REC-"+state, uint64(i+1), state)
		record.Envelope.CorrelationID = "CMD-EXEC-1"
		record.Fields["command_id"] = "CMD-EXEC-1"
		if i > 2 {
			record = executionFeedbackRecord("FB-REC-"+state, uint64(i+1), state, "COMMAND_"+state)
		}
		if err := r.Apply(record); err != nil {
			t.Fatalf("apply %s: %v", state, err)
		}
	}
	got, ok := r.Get("CMD-EXEC-1")
	if !ok {
		t.Fatal("missing reconciled command")
	}
	if got.State != CommandCompletedExec || !got.Terminal {
		t.Fatalf("unexpected terminal state: %+v", got)
	}
	if got.AckSequence != 4 || got.StartSequence != 5 || got.CompletionSequence != 6 {
		t.Fatalf("unexpected milestone sequences: %+v", got)
	}
	if got.VehicleResponse == "" {
		t.Fatalf("expected vehicle response: %+v", got)
	}
	if got.SentAt.IsZero() || got.AcknowledgedAt.IsZero() || got.StartedAt.IsZero() || got.CompletedAt.IsZero() {
		t.Fatalf("expected lifecycle timestamps: %+v", got)
	}
	if got.Transition != "EXECUTING->COMPLETED" {
		t.Fatalf("unexpected transition: %s", got.Transition)
	}
}

func TestCommandExecutionReconciliationFailureStates(t *testing.T) {
	cases := []struct {
		state       string
		terminal    CommandExecutionState
		setupStates []string
	}{
		{"FAILED", CommandFailedExec, []string{"VALIDATED", "QUEUED", "SENT", "ACKNOWLEDGED", "EXECUTING"}},
		{"TIMEOUT", CommandTimeoutExec, []string{"VALIDATED", "QUEUED", "SENT"}},
		{"CANCELLED", CommandCancelledExec, []string{"VALIDATED", "QUEUED"}},
		{"REJECTED", CommandRejectedExec, []string{"VALIDATED"}},
	}
	for _, tc := range cases {
		r := NewCommandExecutionReconciler()
		seq := uint64(1)
		for _, setup := range tc.setupStates {
			setupRecord := executionCommandRecord("SETUP-"+setup, seq, setup)
			setupRecord.Envelope.CorrelationID = "CMD-EXEC-1"
			setupRecord.Fields["command_id"] = "CMD-EXEC-1"
			if err := r.Apply(setupRecord); err != nil {
				t.Fatalf("%s setup %s: %v", tc.state, setup, err)
			}
			seq++
		}
		record := executionFeedbackRecord("FB-"+tc.state, seq, tc.state, "COMMAND_"+tc.state)
		record.Fields["result"] = "failure"
		record.Fields["error_code"] = tc.state + "_001"
		if err := r.Apply(record); err != nil {
			t.Fatalf("%s: %v", tc.state, err)
		}
		got, ok := r.Get("CMD-EXEC-1")
		if !ok {
			t.Fatalf("missing command after %s", tc.state)
		}
		if got.State != tc.terminal || !got.Terminal {
			t.Fatalf("%s produced unexpected state: %+v", tc.state, got)
		}
		if got.ErrorCode != tc.state+"_001" {
			t.Fatalf("%s error code not preserved: %+v", tc.state, got)
		}
	}
}

func TestCommandExecutionReconciliationRejectsInvalidRegression(t *testing.T) {
	r := NewCommandExecutionReconciler()
	states := []string{"VALIDATED", "QUEUED", "SENT", "ACKNOWLEDGED", "EXECUTING", "COMPLETED"}
	for i, state := range states {
		record := executionCommandRecord("SETUP-"+state, uint64(i+1), state)
		record.Envelope.CorrelationID = "CMD-EXEC-1"
		record.Fields["command_id"] = "CMD-EXEC-1"
		if i > 2 {
			record = executionFeedbackRecord("FB-"+state, uint64(i+1), state, "COMMAND_"+state)
		}
		if err := r.Apply(record); err != nil {
			t.Fatalf("setup %s: %v", state, err)
		}
	}
	if err := r.Apply(executionFeedbackRecord("FB-EXEC", 7, "EXECUTING", "COMMAND_EXECUTING")); err == nil {
		t.Fatal("expected terminal regression to fail")
	}
	got, ok := r.Get("CMD-EXEC-1")
	if !ok {
		t.Fatal("missing command")
	}
	if got.State != CommandCompletedExec || got.LastError == "" {
		t.Fatalf("expected completed state with reconciliation error: %+v", got)
	}
}

func TestCommandExecutionReconciliationSnapshotSortedByFeedbackTime(t *testing.T) {
	r := NewCommandExecutionReconciler()
	for i := 0; i < 2; i++ {
		id := "CMD-SORT-" + string(rune('A'+i))
		rec := executionCommandRecord(id, uint64(i+1), "VALIDATED")
		rec.Envelope.CorrelationID = id
		rec.Fields["command_id"] = id
		if err := r.Apply(rec); err != nil {
			t.Fatal(err)
		}
		time.Sleep(2 * time.Millisecond)
	}
	values := r.Snapshot()
	if len(values) != 2 || values[0].LastFeedbackAt.Before(values[1].LastFeedbackAt) {
		t.Fatalf("unexpected snapshot order: %+v", values)
	}
}

func TestCommandExecutionReconciliationIgnoresReceivedLifecycle(t *testing.T) {
	r := NewCommandExecutionReconciler()
	record := executionCommandRecord("CMD-REC-RECEIVED", 1, "RECEIVED")
	if err := r.Apply(record); err != nil {
		t.Fatalf("RECEIVED lifecycle should be ignored by execution reconciliation: %v", err)
	}
	if _, ok := r.Get("CMD-REC-RECEIVED"); ok {
		t.Fatal("RECEIVED lifecycle must not create an execution read model")
	}
}
