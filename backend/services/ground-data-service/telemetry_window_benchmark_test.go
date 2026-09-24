package main

import "testing"

func BenchmarkTelemetrySlidingWindow(b *testing.B) {
	values := make([]float64, 32)
	for i := range values {
		values[i] = float64(i)
	}
	b.ResetTimer()
	w := newTelemetrySlidingWindow(32)
	for i := 0; i < b.N; i++ {
		w.append(uint64(i+1), values[i%len(values)])
	}
}

func BenchmarkTelemetryFullWindowRecompute(b *testing.B) {
	values := make([]float64, 32)
	for i := range values {
		values[i] = float64(i)
	}
	window := make([]float64, 0, 32)
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		value := values[i%len(values)]
		if len(window) == 32 {
			copy(window, window[1:])
			window[31] = value
		} else {
			window = append(window, value)
		}
		sum := 0.0
		for _, sample := range window {
			sum += sample
		}
		_ = sum / float64(len(window))
	}
}
