package main

import (
	"context"
	"errors"
	"sync"
	"testing"
	"time"
)

type retryVehicleExecutor struct {
	mu       sync.Mutex
	calls    int
	failures int
	block    time.Duration
}

func (f *retryVehicleExecutor) Execute(ctx context.Context, _ GroundRecord) (VehicleExecutionResult, error) {
	f.mu.Lock()
	f.calls++
	call := f.calls
	f.mu.Unlock()
	if f.block > 0 {
		select {
		case <-time.After(f.block):
		case <-ctx.Done():
			return VehicleExecutionResult{}, ctx.Err()
		}
	}
	if call <= f.failures {
		return VehicleExecutionResult{}, errors.New("transient vehicle link failure")
	}
	return VehicleExecutionResult{Status: "APPLIED", Message: "executed", Mode: "SURFACE_READY"}, nil
}
func (f *retryVehicleExecutor) Close() error { return nil }

func TestCommandReliabilityRetriesTransientFailure(t *testing.T) {
	fake := &retryVehicleExecutor{failures: 2}
	emit := func(GroundRecord) IngestResult { return IngestResult{Accepted: true, Reason: "accepted"} }
	p := CommandReliabilityPolicy{MaxAttempts: 3, AttemptTimeout: 100 * time.Millisecond, InitialBackoff: time.Millisecond, MaxBackoff: 2 * time.Millisecond}
	o, err := NewCommandExecutionOrchestratorWithPolicy(context.Background(), fake, emit, 1, 8, p)
	if err != nil {
		t.Fatal(err)
	}
	defer o.Close()
	if err := o.Submit(executionCommandRecord("CMD-REL-001", 1, "RECEIVED")); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		if o.Snapshot().Executed == 1 {
			break
		}
		time.Sleep(time.Millisecond)
	}
	if got := o.Snapshot(); got.Executed != 1 || got.Failed != 0 {
		t.Fatalf("unexpected snapshot: %+v", got)
	}
	if fake.calls != 3 {
		t.Fatalf("expected 3 attempts, got %d", fake.calls)
	}
	if got := o.Snapshot(); got.Retries != 2 {
		t.Fatalf("expected 2 retries, got %d", got.Retries)
	}
}

func TestCommandReliabilityTimeoutThenFailure(t *testing.T) {
	fake := &retryVehicleExecutor{block: 50 * time.Millisecond}
	p := CommandReliabilityPolicy{MaxAttempts: 2, AttemptTimeout: 5 * time.Millisecond, InitialBackoff: time.Millisecond, MaxBackoff: time.Millisecond}
	o, err := NewCommandExecutionOrchestratorWithPolicy(context.Background(), fake, func(GroundRecord) IngestResult { return IngestResult{Accepted: true} }, 1, 8, p)
	if err != nil {
		t.Fatal(err)
	}
	defer o.Close()
	if err := o.Submit(executionCommandRecord("CMD-REL-002", 2, "RECEIVED")); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		if o.Snapshot().Failed == 1 {
			break
		}
		time.Sleep(time.Millisecond)
	}
	if got := o.Snapshot(); got.Failed != 1 || got.Executed != 0 {
		t.Fatalf("unexpected snapshot: %+v", got)
	}
	if fake.calls != 2 {
		t.Fatalf("expected 2 timed-out attempts, got %d", fake.calls)
	}
}

func TestCommandReliabilityDoesNotRetryRejectedStatus(t *testing.T) {
	fake := &statusVehicleExecutor{result: VehicleExecutionResult{Status: "REJECTED", Message: "unsafe command"}}
	o, err := NewCommandExecutionOrchestratorWithPolicy(context.Background(), fake, func(GroundRecord) IngestResult { return IngestResult{Accepted: true} }, 1, 8, DefaultCommandReliabilityPolicy())
	if err != nil {
		t.Fatal(err)
	}
	defer o.Close()
	if err := o.Submit(executionCommandRecord("CMD-REL-003", 3, "RECEIVED")); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		if o.Snapshot().Failed == 1 {
			break
		}
		time.Sleep(time.Millisecond)
	}
	if fake.calls != 1 {
		t.Fatalf("expected no retry for rejected command, got %d calls", fake.calls)
	}
}

type statusVehicleExecutor struct {
	mu     sync.Mutex
	calls  int
	result VehicleExecutionResult
}

func (f *statusVehicleExecutor) Execute(context.Context, GroundRecord) (VehicleExecutionResult, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.calls++
	return f.result, nil
}
func (f *statusVehicleExecutor) Close() error { return nil }

func TestCommandReliabilityPolicyBackoffIsBounded(t *testing.T) {
	p := CommandReliabilityPolicy{MaxAttempts: 4, AttemptTimeout: time.Second, InitialBackoff: 10 * time.Millisecond, MaxBackoff: 25 * time.Millisecond}
	if got := p.Backoff(1); got != 10*time.Millisecond {
		t.Fatalf("attempt 1 backoff = %v", got)
	}
	if got := p.Backoff(2); got != 20*time.Millisecond {
		t.Fatalf("attempt 2 backoff = %v", got)
	}
	if got := p.Backoff(3); got != 25*time.Millisecond {
		t.Fatalf("attempt 3 backoff = %v", got)
	}
	if got := p.Backoff(4); got != 25*time.Millisecond {
		t.Fatalf("attempt 4 backoff = %v", got)
	}
}

func TestCommandReliabilityEmitsRetryEvent(t *testing.T) {
	fake := &retryVehicleExecutor{failures: 1}
	var mu sync.Mutex
	var events []GroundRecord
	emit := func(r GroundRecord) IngestResult {
		mu.Lock()
		events = append(events, r)
		mu.Unlock()
		return IngestResult{Accepted: true, Reason: "accepted"}
	}
	p := CommandReliabilityPolicy{MaxAttempts: 2, AttemptTimeout: 100 * time.Millisecond, InitialBackoff: time.Millisecond, MaxBackoff: time.Millisecond}
	o, err := NewCommandExecutionOrchestratorWithPolicy(context.Background(), fake, emit, 1, 8, p)
	if err != nil {
		t.Fatal(err)
	}
	defer o.Close()
	if err := o.Submit(executionCommandRecord("CMD-REL-004", 4, "RECEIVED")); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		if o.Snapshot().Executed == 1 {
			break
		}
		time.Sleep(time.Millisecond)
	}
	mu.Lock()
	defer mu.Unlock()
	found := false
	for _, r := range events {
		if r.Envelope.Kind == KindEvent && r.Fields["event_type"] == "COMMAND_RETRY" && r.Fields["command_id"] == "CMD-REL-004" {
			found = true
			break
		}
	}
	if !found {
		t.Fatal("expected COMMAND_RETRY event")
	}
	if snap := o.Snapshot(); snap.Retries != 1 || snap.MaxAttempts != 2 {
		t.Fatalf("unexpected retry snapshot: %+v", snap)
	}
}
