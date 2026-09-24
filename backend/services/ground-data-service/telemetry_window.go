package main

import (
	"fmt"
	"strconv"
)

const (
	currentTelemetryWindowVersion = "v0.9.110.1"
	defaultTelemetryWindowSize    = 32
)

// TelemetryWindowSummary is the operator-facing rolling view for one metric.
// It is derived state; durable telemetry history remains in the repository.
type TelemetryWindowSummary struct {
	Metric        string  `json:"metric"`
	Count         int     `json:"count"`
	Capacity      int     `json:"capacity"`
	Average       float64 `json:"average"`
	Latest        float64 `json:"latest"`
	Delta         float64 `json:"delta"`
	LastSequence  uint64  `json:"last_sequence"`
	WindowVersion string  `json:"window_version"`
}

// telemetrySlidingWindow is a fixed-size ring buffer. Appending and evicting
// the oldest sample are O(1); the running sum makes rolling average O(1).
// Sequence ordering prevents late telemetry from corrupting the window.
type telemetrySlidingWindow struct {
	values       []float64
	head         int
	count        int
	sum          float64
	lastSequence uint64
	latest       float64
	first        float64
}

func newTelemetrySlidingWindow(capacity int) *telemetrySlidingWindow {
	if capacity <= 0 {
		capacity = defaultTelemetryWindowSize
	}
	return &telemetrySlidingWindow{values: make([]float64, capacity)}
}

func (w *telemetrySlidingWindow) append(sequence uint64, value float64) bool {
	if w == nil || len(w.values) == 0 || sequence <= w.lastSequence {
		return false
	}
	index := (w.head + w.count) % len(w.values)
	if w.count == len(w.values) {
		w.sum -= w.values[w.head]
		w.values[w.head] = value
		w.sum += value
		w.head = (w.head + 1) % len(w.values)
		w.first = w.values[w.head]
	} else {
		w.values[index] = value
		w.count++
		w.sum += value
		if w.count == 1 {
			w.first = value
		}
	}
	w.latest = value
	w.lastSequence = sequence
	return true
}

func (w *telemetrySlidingWindow) summary(metric string) TelemetryWindowSummary {
	if w == nil {
		return TelemetryWindowSummary{Metric: metric, Capacity: defaultTelemetryWindowSize, WindowVersion: currentTelemetryWindowVersion}
	}
	average := 0.0
	if w.count > 0 {
		average = w.sum / float64(w.count)
	}
	return TelemetryWindowSummary{
		Metric:        metric,
		Count:         w.count,
		Capacity:      len(w.values),
		Average:       average,
		Latest:        w.latest,
		Delta:         w.latest - w.first,
		LastSequence:  w.lastSequence,
		WindowVersion: currentTelemetryWindowVersion,
	}
}

func parseTelemetryWindowValue(raw string) (float64, error) {
	value, err := strconv.ParseFloat(raw, 64)
	if err != nil {
		return 0, fmt.Errorf("telemetry window value %q is not numeric: %w", raw, err)
	}
	return value, nil
}
