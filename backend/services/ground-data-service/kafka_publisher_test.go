package main

import (
	"context"
	"errors"
	"testing"
)

type fakePublisher struct {
	records []GroundRecord
	err     error
}

func (f *fakePublisher) Publish(_ context.Context, record GroundRecord) error {
	if f.err != nil {
		return f.err
	}
	f.records = append(f.records, record)
	return nil
}

func (f *fakePublisher) Close() error { return nil }

func TestServicePublishesAfterDurableAppend(t *testing.T) {
	store := newMemoryRecordStore()
	publisher := &fakePublisher{}
	service := newService(100, store, nil, publisher)
	record := testGroundRecord("KAFKA-001", KindTelemetry, 1)

	result := service.Ingest(record)
	if !result.Accepted || !result.StreamPublished || result.StreamError != "" {
		t.Fatalf("unexpected result: %+v", result)
	}
	if len(publisher.records) != 1 || publisher.records[0].Envelope.RecordID != "KAFKA-001" {
		t.Fatalf("publisher received %+v", publisher.records)
	}
	stored, _ := store.LoadAll()
	if len(stored) != 1 {
		t.Fatalf("expected durable append before publish, got %d", len(stored))
	}
}

func TestServiceAcceptsDurableRecordWhenKafkaUnavailable(t *testing.T) {
	store := newMemoryRecordStore()
	publisher := &fakePublisher{err: errors.New("broker unavailable")}
	service := newService(100, store, nil, publisher)
	result := service.Ingest(testGroundRecord("KAFKA-002", KindTelemetry, 1))
	if !result.Accepted || result.StreamPublished || result.StreamError == "" {
		t.Fatalf("unexpected result: %+v", result)
	}
	stored, _ := store.LoadAll()
	if len(stored) != 1 {
		t.Fatalf("expected durable record despite stream failure, got %d", len(stored))
	}
}
