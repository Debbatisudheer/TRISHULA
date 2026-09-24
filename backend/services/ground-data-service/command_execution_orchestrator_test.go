package main

import (
	"context"
	"sync"
	"testing"
	"time"
)

type fakeVehicleExecutor struct {
	mu     sync.Mutex
	calls  int
	result VehicleExecutionResult
}

func (f *fakeVehicleExecutor) Execute(_ context.Context, _ GroundRecord) (VehicleExecutionResult, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls++
	return f.result, nil
}
func (f *fakeVehicleExecutor) Close() error { return nil }

func TestCommandExecutionOrchestratorHappyPath(t *testing.T) {
	fake := &fakeVehicleExecutor{result: VehicleExecutionResult{Status: "APPLIED", Message: "rover motion stopped", Mode: "SURFACE_READY", BatterySOC: 0.91}}
	var emitted []GroundRecord
	var mu sync.Mutex
	emit := func(r GroundRecord) IngestResult {
		mu.Lock()
		emitted = append(emitted, r)
		mu.Unlock()
		return IngestResult{Accepted: true, Reason: "accepted"}
	}
	orch, err := NewCommandExecutionOrchestrator(context.Background(), fake, emit, 1, 8)
	if err != nil {
		t.Fatal(err)
	}
	defer orch.Close()
	record := executionCommandRecord("CMD-ORCH-001", 1, "RECEIVED")
	if err := orch.Submit(record); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		snap := orch.Snapshot()
		if snap.Executed == 1 {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	snap := orch.Snapshot()
	if snap.Executed != 1 || snap.Failed != 0 {
		t.Fatalf("unexpected orchestrator snapshot: %+v", snap)
	}
	mu.Lock()
	defer mu.Unlock()
	if len(emitted) < 5 {
		t.Fatalf("expected lifecycle + feedback records, got %d", len(emitted))
	}
	if fake.calls != 1 {
		t.Fatalf("expected one vehicle execution, got %d", fake.calls)
	}
}

func TestParseVehicleBridgeResponse(t *testing.T) {
	line := "RESULT\tAPPLIED\t726f766572206465706c6f796d656e7420616e64207375726661636520696e697469616c697a6174696f6e206170706c696564\tSURFACE_READY\t0\t0\t0\t0\t1\t1\t1\t1\t0\t1\t0\t434d442d4252494447452d54455354\t53544152545f524f564552"
	result, err := parseVehicleBridgeResponse(line)
	if err != nil {
		t.Fatalf("expected bridge response to parse, got %v", err)
	}
	if result.Status != "APPLIED" || result.Mode != "SURFACE_READY" {
		t.Fatalf("unexpected result: %+v", result)
	}
	if result.PhysicalSteps != 0 || !result.Deployed || !result.MastReady || !result.LocalizationValid || result.HazardDetected || !result.DriveHealthy {
		t.Fatalf("unexpected physical state: %+v", result)
	}
}
