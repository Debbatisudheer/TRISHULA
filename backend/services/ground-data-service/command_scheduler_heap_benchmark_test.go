package main

import (
	"container/heap"
	"sort"
	"strconv"
	"testing"
	"time"
)

func buildBenchmarkSchedules(n int) []CommandSchedule {
	base := time.Unix(1000, 0).UTC()
	schedules := make([]CommandSchedule, n)
	for i := 0; i < n; i++ {
		// Reverse insertion order makes the benchmark exercise actual ordering work.
		schedules[i] = CommandSchedule{
			ScheduleID:  strconv.Itoa(n - i),
			ScheduledAt: base.Add(time.Duration(n-i) * time.Second),
			State:       CommandSchedulePending,
		}
	}
	return schedules
}

func BenchmarkCommandSchedulerHeapSelection(b *testing.B) {
	schedules := buildBenchmarkSchedules(10000)
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		h := make(commandScheduleHeap, 0, len(schedules))
		for _, schedule := range schedules {
			heap.Push(&h, schedule)
		}
		_ = heap.Pop(&h).(CommandSchedule)
	}
}

func BenchmarkCommandSchedulerSortSelection(b *testing.B) {
	schedules := buildBenchmarkSchedules(10000)
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		sorted := append([]CommandSchedule(nil), schedules...)
		sort.Slice(sorted, func(i, j int) bool {
			if sorted[i].ScheduledAt.Equal(sorted[j].ScheduledAt) {
				return sorted[i].ScheduleID < sorted[j].ScheduleID
			}
			return sorted[i].ScheduledAt.Before(sorted[j].ScheduledAt)
		})
		_ = sorted[0]
	}
}
