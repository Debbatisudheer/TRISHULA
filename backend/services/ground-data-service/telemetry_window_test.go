package main

import "testing"

func TestTelemetrySlidingWindowMaintainsRollingAverage(t *testing.T) {
	w := newTelemetrySlidingWindow(3)
	for i, value := range []float64{10, 20, 30} {
		if !w.append(uint64(i+1), value) {
			t.Fatalf("append %d rejected", i+1)
		}
	}
	if got := w.summary("battery"); got.Count != 3 || got.Average != 20 || got.Latest != 30 || got.Delta != 20 {
		t.Fatalf("unexpected initial window: %+v", got)
	}
	if !w.append(4, 40) {
		t.Fatal("expected eviction append to succeed")
	}
	got := w.summary("battery")
	if got.Count != 3 || got.Average != 30 || got.Latest != 40 || got.Delta != 20 {
		t.Fatalf("unexpected rolling window after eviction: %+v", got)
	}
}

func TestTelemetrySlidingWindowRejectsLateSequence(t *testing.T) {
	w := newTelemetrySlidingWindow(3)
	if !w.append(10, 80) {
		t.Fatal("first sample rejected")
	}
	if w.append(9, 60) {
		t.Fatal("late sample should be rejected")
	}
	got := w.summary("battery")
	if got.Count != 1 || got.Latest != 80 || got.LastSequence != 10 {
		t.Fatalf("late sample corrupted window: %+v", got)
	}
}
