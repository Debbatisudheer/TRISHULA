package main

import "testing"

func commandRecord(id string, seq uint64, lifecycle string) GroundRecord {
	record := GroundRecord{
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
			MissionTimestamp: 1_000_000,
			SequenceNumber:   seq,
			Quality:          1.0,
			PayloadSchema:    "command.v1",
		},
		Fields: map[string]string{
			"command": "STOP_ROVER",
			"target":  "ROVER-01",
		},
	}
	record.Fields["command_id"] = id
	if lifecycle != "" {
		record.Fields["lifecycle"] = lifecycle
	}
	return record
}

func TestCommandEngineLifecycleTransitions(t *testing.T) {
	engine := NewCommandEngine()
	states := []CommandLifecycle{
		CommandValidated,
		CommandSent,
		CommandAcknowledged,
		CommandExecuting,
		CommandCompleted,
	}
	for i, expected := range states {
		evaluation, err := engine.Evaluate(commandRecord("CMD-1", uint64(i+1), string(expected)))
		if err != nil {
			t.Fatalf("evaluate %s: %v", expected, err)
		}
		if evaluation.Lifecycle != expected {
			t.Fatalf("expected lifecycle %s, got %s", expected, evaluation.Lifecycle)
		}
		if i > 0 && evaluation.Previous != states[i-1] {
			t.Fatalf("expected previous lifecycle %s, got %s", states[i-1], evaluation.Previous)
		}
	}
	evaluation, ok := func() (CommandLifecycleEvaluation, bool) {
		for _, item := range engine.Snapshot() {
			if item.CommandID == "CMD-1" {
				return item, true
			}
		}
		return CommandLifecycleEvaluation{}, false
	}()
	if !ok || evaluation.Lifecycle != CommandCompleted || !evaluation.Terminal {
		t.Fatalf("expected terminal completed state, got ok=%v evaluation=%+v", ok, evaluation)
	}
}

func TestCommandEngineRejectsInvalidTransition(t *testing.T) {
	engine := NewCommandEngine()
	if _, err := engine.Evaluate(commandRecord("CMD-2", 1, "SENT")); err == nil {
		t.Fatal("expected direct RECEIVED->SENT transition to fail")
	}
	if _, err := engine.Evaluate(commandRecord("CMD-2", 2, "VALIDATED")); err != nil {
		t.Fatalf("expected initial validation state: %v", err)
	}
	if _, err := engine.Evaluate(commandRecord("CMD-2", 3, "COMPLETED")); err == nil {
		t.Fatal("expected VALIDATED->COMPLETED transition to fail")
	}
}

func TestCommandEngineTerminalStateCannotAdvance(t *testing.T) {
	engine := NewCommandEngine()
	sequence := []CommandLifecycle{CommandValidated, CommandSent, CommandFailed}
	for i, state := range sequence {
		if _, err := engine.Evaluate(commandRecord("CMD-3", uint64(i+1), string(state))); err != nil {
			t.Fatalf("state %s: %v", state, err)
		}
	}
	if _, err := engine.Evaluate(commandRecord("CMD-3", 4, "EXECUTING")); err == nil {
		t.Fatal("expected terminal FAILED command to reject further transition")
	}
}

func TestCommandEngineUsesStableCommandIDAcrossRecords(t *testing.T) {
	engine := NewCommandEngine()
	first := commandRecord("CMD-7-A", 1, "VALIDATED")
	first.Fields["command_id"] = "CMD-7"
	second := commandRecord("CMD-7-B", 2, "SENT")
	second.Fields["command_id"] = "CMD-7"
	evaluation, err := engine.Evaluate(first)
	if err != nil || evaluation.Lifecycle != CommandValidated {
		t.Fatalf("first lifecycle: %+v err=%v", evaluation, err)
	}
	evaluation, err = engine.Evaluate(second)
	if err != nil || evaluation.Previous != CommandValidated || evaluation.Lifecycle != CommandSent {
		t.Fatalf("stable command id did not preserve lifecycle: %+v err=%v", evaluation, err)
	}
}

func TestCommandEngineSameStateIsNoop(t *testing.T) {
	engine := NewCommandEngine()
	if _, err := engine.Evaluate(commandRecord("CMD-4", 1, "VALIDATED")); err != nil {
		t.Fatal(err)
	}
	second, err := engine.Evaluate(commandRecord("CMD-4", 2, "VALIDATED"))
	if err != nil {
		t.Fatal(err)
	}
	if second.Transition != "VALIDATED->VALIDATED:NOOP" {
		t.Fatalf("unexpected noop transition: %s", second.Transition)
	}
}

func TestCommandEngineDefaultLifecycleIsValidated(t *testing.T) {
	engine := NewCommandEngine()
	evaluation, err := engine.Evaluate(commandRecord("CMD-5", 1, ""))
	if err != nil {
		t.Fatalf("default lifecycle evaluation failed: %v", err)
	}
	if evaluation.Lifecycle != CommandValidated {
		t.Fatalf("expected default VALIDATED, got %s", evaluation.Lifecycle)
	}
}

func TestCommandProcessingOutputUsesLifecycleEngine(t *testing.T) {
	engine := NewCommandEngine()
	validated := commandRecord("CMD-6-A", 1, "VALIDATED")
	validated.Fields["command_id"] = "CMD-6"
	if _, err := processCommandEngineOutput(validated, engine); err != nil {
		t.Fatalf("validated lifecycle: %v", err)
	}
	record := commandRecord("CMD-6-B", 2, "SENT")
	record.Fields["command_id"] = "CMD-6"
	result, err := processCommandEngineOutput(record, engine)
	if err != nil {
		t.Fatalf("process command output: %v", err)
	}
	if result.Operation != "command-lifecycle:STOP_ROVER:SENT" {
		t.Fatalf("unexpected operation: %s", result.Operation)
	}
	if result.Attributes["lifecycle"] != "SENT" || result.Attributes["target"] != "ROVER-01" {
		t.Fatalf("unexpected attributes: %+v", result.Attributes)
	}
}

func TestCommandEngineSupportsQueuedTimeoutAndCancelledLifecycle(t *testing.T) {
	engine := NewCommandEngine()
	sequence := []CommandLifecycle{CommandValidated, CommandQueued, CommandSent, CommandAcknowledged, CommandExecuting, CommandTimeout}
	for i, state := range sequence {
		if _, err := engine.Evaluate(commandRecord("CMD-8", uint64(i+1), string(state))); err != nil {
			t.Fatalf("state %s: %v", state, err)
		}
	}
	if _, err := engine.Evaluate(commandRecord("CMD-9", 1, string(CommandValidated))); err != nil {
		t.Fatal(err)
	}
	if _, err := engine.Evaluate(commandRecord("CMD-9", 2, string(CommandCancelled))); err != nil {
		t.Fatalf("cancelled lifecycle: %v", err)
	}
}
