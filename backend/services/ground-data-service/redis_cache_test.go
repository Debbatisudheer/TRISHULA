package main

import (
	"context"
	"testing"
)

type fakeStateCache struct {
	state    CurrentState
	setCalls int
}

func (f *fakeStateCache) SetSnapshot(_ context.Context, state CurrentState) error {
	f.state = state
	f.setCalls++
	return nil
}
func (f *fakeStateCache) GetSnapshot(_ context.Context) (CurrentState, bool, error) {
	return f.state, f.setCalls > 0, nil
}
func (f *fakeStateCache) Close() error { return nil }

func TestServiceProjectsCurrentStateToCache(t *testing.T) {
	cache := &fakeStateCache{}
	service := NewServiceWithStoreAndCache(10, nil, cache)
	record := sampleRecord("cache-1", 7, KindTelemetry)
	result := service.Ingest(record)
	if !result.Accepted {
		t.Fatalf("expected accepted record: %+v", result)
	}
	if cache.setCalls != 2 {
		t.Fatalf("expected initial+ingest cache writes, got %d", cache.setCalls)
	}
	cached, ok, err := cache.GetSnapshot(context.Background())
	if err != nil || !ok {
		t.Fatalf("expected cached state, ok=%v err=%v", ok, err)
	}
	if cached.LastSequence["ROVER-01"] != 7 {
		t.Fatalf("unexpected cached sequence: %+v", cached.LastSequence)
	}
	if cached.Counters[KindTelemetry] != 1 {
		t.Fatalf("unexpected cached counters: %+v", cached.Counters)
	}
}
