package main

import (
	"context"
	"errors"
	"sync"
	"testing"
	"time"
)

type dlqTestExecutor struct {
	mu    sync.Mutex
	calls int
	err   error
}

func (e *dlqTestExecutor) Execute(context.Context, GroundRecord) (VehicleExecutionResult, error) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.calls++
	if e.err != nil {
		return VehicleExecutionResult{}, e.err
	}
	return VehicleExecutionResult{Status: "APPLIED", Message: "replay succeeded"}, nil
}
func (e *dlqTestExecutor) Close() error { return nil }

func TestCommandDeadLetterQueueEnqueueReplayResolve(t *testing.T) {
	var emitted []GroundRecord
	var mu sync.Mutex
	emit := func(r GroundRecord) IngestResult {
		mu.Lock()
		emitted = append(emitted, r)
		mu.Unlock()
		return IngestResult{Accepted: true}
	}
	q := NewCommandDeadLetterQueue(emit, nil)
	record := executionCommandRecord("CMD-DLQ-001", 1, "RECEIVED")
	entry, err := q.Enqueue(record, CommandTimeoutExec, 3, errors.New("attempt timeout"))
	if err != nil {
		t.Fatal(err)
	}
	if entry.State != CommandDLQPending || entry.Attempts != 3 {
		t.Fatalf("unexpected DLQ entry: %+v", entry)
	}
	if got := q.Snapshot().Pending; got != 1 {
		t.Fatalf("expected one pending entry, got %d", got)
	}

	var replayed bool
	q.SetReplay(func(GroundRecord) error { replayed = true; return nil })
	replayedEntry, err := q.Replay(context.Background(), entry.DeadLetterID)
	if err != nil {
		t.Fatal(err)
	}
	if !replayed || replayedEntry.State != CommandDLQReplayed {
		t.Fatalf("expected replayed entry, got %+v", replayedEntry)
	}
	if _, err := q.Replay(context.Background(), entry.DeadLetterID); err == nil {
		t.Fatal("expected duplicate replay to be rejected")
	}

	resolvedEntry, err := q.Resolve(context.Background(), entry.DeadLetterID)
	if err != nil {
		t.Fatal(err)
	}
	if resolvedEntry.State != CommandDLQResolved {
		t.Fatalf("expected resolved entry, got %+v", resolvedEntry)
	}
	mu.Lock()
	defer mu.Unlock()
	if len(emitted) != 3 {
		t.Fatalf("expected enqueue/replay/resolve events, got %d", len(emitted))
	}
}

func TestCommandExecutionOrchestratorDeadLettersExhaustedFailure(t *testing.T) {
	executor := &dlqTestExecutor{err: errors.New("link unavailable")}
	q := NewCommandDeadLetterQueue(func(GroundRecord) IngestResult { return IngestResult{Accepted: true} }, nil)
	policy := CommandReliabilityPolicy{MaxAttempts: 2, AttemptTimeout: time.Second, InitialBackoff: time.Millisecond, MaxBackoff: time.Millisecond}
	orch, err := NewCommandExecutionOrchestratorWithPolicy(context.Background(), executor, func(GroundRecord) IngestResult { return IngestResult{Accepted: true} }, 1, 8, policy)
	if err != nil {
		t.Fatal(err)
	}
	defer orch.Close()
	orch.SetDeadLetterQueue(q)
	record := executionCommandRecord("CMD-DLQ-002", 2, "RECEIVED")
	if err := orch.Submit(record); err != nil {
		t.Fatal(err)
	}
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if orch.Snapshot().Failed == 1 {
			break
		}
		time.Sleep(5 * time.Millisecond)
	}
	if executor.calls != 2 {
		t.Fatalf("expected two attempts, got %d", executor.calls)
	}
	entries := q.List("TRISHULA")
	if len(entries) != 1 || entries[0].State != CommandDLQPending || entries[0].ExecutionState != CommandFailedExec {
		t.Fatalf("expected one pending failed DLQ entry, got %+v", entries)
	}
}
