package main

import (
	"container/heap"
	"testing"
	"time"
)

func TestCommandScheduleHeapOrdersByScheduledTime(t *testing.T) {
	base := time.Unix(1000, 0).UTC()
	h := commandScheduleHeap{}
	heap.Init(&h)
	heap.Push(&h, CommandSchedule{ScheduleID: "SCH-003", ScheduledAt: base.Add(30 * time.Second)})
	heap.Push(&h, CommandSchedule{ScheduleID: "SCH-001", ScheduledAt: base.Add(10 * time.Second)})
	heap.Push(&h, CommandSchedule{ScheduleID: "SCH-002", ScheduledAt: base.Add(20 * time.Second)})

	expected := []string{"SCH-001", "SCH-002", "SCH-003"}
	for _, id := range expected {
		got := heap.Pop(&h).(CommandSchedule).ScheduleID
		if got != id {
			t.Fatalf("heap order=%s, want %s", got, id)
		}
	}
}

func TestCommandScheduleHeapTieBreaksByScheduleID(t *testing.T) {
	at := time.Unix(1000, 0).UTC()
	h := commandScheduleHeap{}
	heap.Init(&h)
	heap.Push(&h, CommandSchedule{ScheduleID: "SCH-B", ScheduledAt: at})
	heap.Push(&h, CommandSchedule{ScheduleID: "SCH-A", ScheduledAt: at})

	if got := heap.Pop(&h).(CommandSchedule).ScheduleID; got != "SCH-A" {
		t.Fatalf("tie-break result=%s, want SCH-A", got)
	}
}

func TestCommandSchedulerStaleCancelledHeapEntryIsIgnored(t *testing.T) {
	scheduler := &CommandScheduler{
		schedules: make(map[string]CommandSchedule),
		pending:   make(commandScheduleHeap, 0),
	}
	at := time.Unix(1000, 0).UTC()
	cancelled := CommandSchedule{ScheduleID: "SCH-CANCEL", ScheduledAt: at, State: CommandScheduleCancelled}
	live := CommandSchedule{ScheduleID: "SCH-LIVE", ScheduledAt: at.Add(time.Second), State: CommandSchedulePending}
	scheduler.schedules[cancelled.ScheduleID] = cancelled
	scheduler.schedules[live.ScheduleID] = live
	heap.Push(&scheduler.pending, cancelled)
	heap.Push(&scheduler.pending, live)

	scheduler.removeStaleHeapTopLocked()
	if len(scheduler.pending) != 1 {
		t.Fatalf("pending heap length=%d, want 1", len(scheduler.pending))
	}
	if got := scheduler.pending[0].ScheduleID; got != "SCH-LIVE" {
		t.Fatalf("live heap head=%s, want SCH-LIVE", got)
	}
}
