package main

import (
	"context"
	"testing"
	"time"
)

func TestCommandSchedulerCreateAndCancel(t *testing.T) {
	service := NewService(100)
	dispatcher := NewMissionCommandDispatcher(service, "GROUND-OPS-01")
	scheduler, err := NewCommandScheduler(context.Background(), service, dispatcher, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	defer scheduler.Close()

	scheduled, err := scheduler.Create("TRISHULA", CommandScheduleRequest{
		ScheduleID: "SCH-085-001", CommandID: "CMD-085-001", Command: "START_ROVER",
		Target: "ROVER-01", SequenceNumber: 501, ScheduledAt: time.Now().UTC().Add(30 * time.Second),
	})
	if err != nil {
		t.Fatal(err)
	}
	if scheduled.State != CommandSchedulePending {
		t.Fatalf("unexpected state: %s", scheduled.State)
	}
	if got := scheduler.Snapshot().Pending; got != 1 {
		t.Fatalf("pending=%d", got)
	}

	cancelled, err := scheduler.Cancel("SCH-085-001")
	if err != nil {
		t.Fatal(err)
	}
	if cancelled.State != CommandScheduleCancelled {
		t.Fatalf("unexpected cancel state: %s", cancelled.State)
	}
	if got := scheduler.Snapshot().Cancelled; got != 1 {
		t.Fatalf("cancelled=%d", got)
	}
	if got := scheduler.Snapshot().Pending; got != 0 {
		t.Fatalf("pending after cancel=%d", got)
	}
}

func TestCommandSchedulerRejectsDuplicateCommand(t *testing.T) {
	service := NewService(100)
	dispatcher := NewMissionCommandDispatcher(service, "GROUND-OPS-01")
	scheduler, err := NewCommandScheduler(context.Background(), service, dispatcher, time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	defer scheduler.Close()
	at := time.Now().UTC().Add(time.Minute)
	_, err = scheduler.Create("TRISHULA", CommandScheduleRequest{ScheduleID: "SCH-085-002", CommandID: "CMD-085-002", Command: "START_ROVER", Target: "ROVER-01", SequenceNumber: 502, ScheduledAt: at})
	if err != nil {
		t.Fatal(err)
	}
	if _, err = scheduler.Create("TRISHULA", CommandScheduleRequest{ScheduleID: "SCH-085-003", CommandID: "CMD-085-002", Command: "START_ROVER", Target: "ROVER-01", SequenceNumber: 503, ScheduledAt: at}); err == nil {
		t.Fatal("expected duplicate command rejection")
	}
}
