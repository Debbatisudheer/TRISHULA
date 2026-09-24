package main

import (
	"context"
	"testing"
	"time"
)

func TestMissionServiceListIsDeterministic(t *testing.T) {
	router := NewRecordRouter()
	first := sampleRecord("MS-2", 2, KindTelemetry)
	first.Envelope.MissionID = "MISSION-B"
	second := sampleRecord("MS-1", 1, KindTelemetry)
	second.Envelope.MissionID = "MISSION-A"
	if err := router.Route(first); err != nil {
		t.Fatal(err)
	}
	if err := router.Route(second); err != nil {
		t.Fatal(err)
	}

	svc := NewMissionService(router, nil)
	got := svc.List(context.Background(), time.Unix(3, 0))
	if len(got) != 2 {
		t.Fatalf("expected 2 missions, got %d", len(got))
	}
	if got[0].MissionID != "MISSION-A" || got[1].MissionID != "MISSION-B" {
		t.Fatalf("missions not sorted deterministically: %+v", got)
	}
	if got[0].MissionServiceVersion != currentMissionServiceVersion {
		t.Fatalf("unexpected service version: %+v", got[0])
	}
}

func TestMissionServiceGetAggregatesStateHealthFreshnessAndConsistency(t *testing.T) {
	router := NewRecordRouter()
	record := sampleRecord("MS-GET-1", 7, KindTelemetry)
	record.Envelope.MissionID = "TRISHULA"
	record.Envelope.MissionTimestamp = 100
	if err := router.Route(record); err != nil {
		t.Fatal(err)
	}
	repo := syncRepository{records: []GroundRecord{record}}
	svc := NewMissionService(router, repo)

	detail, found, err := svc.Get(context.Background(), "TRISHULA", time.Unix(100, 0))
	if err != nil {
		t.Fatal(err)
	}
	if !found {
		t.Fatal("expected mission to be found")
	}
	if detail.State.Vehicles["ROVER-01"].LastSequence != 7 {
		t.Fatalf("unexpected vehicle state: %+v", detail.State.Vehicles)
	}
	if detail.Health.MissionID != "TRISHULA" {
		t.Fatalf("unexpected health projection: %+v", detail.Health)
	}
	if detail.Freshness.MissionID != "TRISHULA" {
		t.Fatalf("unexpected freshness projection: %+v", detail.Freshness)
	}
	if detail.Consistency == nil || detail.Consistency.Status != "IN_SYNC" {
		t.Fatalf("unexpected consistency projection: %+v", detail.Consistency)
	}
}

func TestMissionServiceGetNotFound(t *testing.T) {
	svc := NewMissionService(NewRecordRouter(), nil)
	_, found, err := svc.Get(context.Background(), "DOES-NOT-EXIST", time.Unix(1, 0))
	if err != nil {
		t.Fatal(err)
	}
	if found {
		t.Fatal("expected unknown mission to be absent")
	}
}

func TestMissionServiceSynchronize(t *testing.T) {
	router := NewRecordRouter()
	old := sampleRecord("LIVE-1", 1, KindTelemetry)
	if err := router.Route(old); err != nil {
		t.Fatal(err)
	}
	repo := syncRepository{records: []GroundRecord{sampleRecord("DURABLE-9", 9, KindTelemetry)}}
	svc := NewMissionService(router, repo)

	result, found, err := svc.Synchronize(context.Background(), "TRISHULA", time.Unix(10, 0))
	if err != nil {
		t.Fatal(err)
	}
	if !found || result.Status != "SYNCHRONIZED" {
		t.Fatalf("unexpected synchronization result: %+v", result)
	}
	mission, ok := router.Mission("TRISHULA")
	if !ok || mission.Vehicles["ROVER-01"].LastSequence != 9 {
		t.Fatalf("mission was not synchronized: %+v", mission)
	}
}
