package main

import (
	"context"
	"testing"
	"time"
)

func operationsRecord(id string, seq uint64, kind DataKind) GroundRecord {
	record := sampleRecord(id, seq, kind)
	record.Envelope.MissionID = "TRISHULA"
	record.Envelope.MissionTimestamp = 1_000_000 + seq
	switch kind {
	case KindTelemetry:
		record.Fields = map[string]string{"metric": "battery", "value": "85", "unit": "%"}
	case KindScience:
		record.Fields = map[string]string{"instrument": "LIBS", "target": "TARGET-7", "measurement": "abundance"}
	case KindEvent:
		record.Fields = map[string]string{"event_type": "THERMAL_WARNING", "severity": "WARNING", "event_class": "FAULT", "fault_code": "THERMAL"}
	case KindCommand:
		record.Fields = map[string]string{"command_id": id, "command": "STOP_ROVER", "target": "ROVER-01", "lifecycle": "COMPLETED"}
	case KindFile:
		record.Fields = map[string]string{"filename": "science.dat", "size_bytes": "4096", "media_type": "science-data"}
	}
	return record
}

func TestMissionOperationsBuildsCommandBoardAndTimeline(t *testing.T) {
	router := NewRecordRouter()
	records := []GroundRecord{
		operationsRecord("OP-T", 1, KindTelemetry),
		operationsRecord("CMD-1", 2, KindCommand),
		operationsRecord("EV-1", 3, KindEvent),
		operationsRecord("SCI-1", 4, KindScience),
	}
	// Route the non-command records to build the live mission projection.
	// The command board is a durable-history read model and intentionally
	// accepts a latest COMPLETED snapshot without requiring the full lifecycle
	// transition chain to be replayed through CommandEngine here.
	for _, record := range records {
		if record.Envelope.Kind == KindCommand {
			continue
		}
		if err := router.Route(record); err != nil {
			t.Fatal(err)
		}
	}
	repo := syncRepository{records: records}
	svc := NewMissionOperationsService(router, repo)
	view, found, err := svc.Operations(context.Background(), "TRISHULA", time.Unix(100, 0), 10)
	if err != nil {
		t.Fatal(err)
	}
	if !found {
		t.Fatal("expected mission operations view")
	}
	if view.MissionOperationsVersion != currentMissionOperationsVersion {
		t.Fatalf("unexpected operations version: %s", view.MissionOperationsVersion)
	}
	if view.Commands.UniqueCommands != 1 || view.Commands.CompletedCommands != 1 {
		t.Fatalf("unexpected command board: %+v", view.Commands)
	}
	if len(view.Timeline) != 4 {
		t.Fatalf("expected 4 timeline entries, got %d", len(view.Timeline))
	}
	if view.Timeline[0].RecordID != "SCI-1" {
		t.Fatalf("expected newest record first, got %+v", view.Timeline)
	}
	if view.Vehicles[0].LastSequence != 4 {
		t.Fatalf("unexpected vehicle summary: %+v", view.Vehicles[0])
	}
}

func TestMissionOperationsTimelineLimitAndCommandLatestState(t *testing.T) {
	router := NewRecordRouter()
	records := []GroundRecord{
		operationsRecord("CMD-1-R", 5, KindCommand),
		operationsRecord("CMD-2", 6, KindCommand),
		operationsRecord("CMD-1-C", 7, KindCommand),
	}
	records[0].Fields["lifecycle"] = "RECEIVED"
	records[2].Fields["command_id"] = "CMD-1"
	records[2].Fields["lifecycle"] = "COMPLETED"
	// This test exercises the durable command read model directly. The records
	// intentionally represent lifecycle snapshots, so they are not routed
	// through the live CommandEngine transition validator.
	repo := syncRepository{records: records}
	svc := NewMissionOperationsService(router, repo)
	timeline, err := svc.Timeline(context.Background(), "TRISHULA", 2)
	if err != nil {
		t.Fatal(err)
	}
	if len(timeline) != 2 {
		t.Fatalf("expected timeline limit 2, got %d", len(timeline))
	}
	board, err := svc.commandOperations(context.Background(), "TRISHULA")
	if err != nil {
		t.Fatal(err)
	}
	// The durable read model contains three distinct command IDs:
	// CMD-1-R (RECEIVED), CMD-2 (COMPLETED), and CMD-1 (COMPLETED).
	// CMD-1-C is a later lifecycle snapshot of CMD-1 because its explicit
	// command_id is "CMD-1". Therefore the expected aggregate is three
	// unique commands: two terminal/completed and one active/received.
	if board.UniqueCommands != 3 || board.CompletedCommands != 2 || board.ActiveCommands != 1 {
		t.Fatalf("unexpected command aggregation: %+v", board)
	}
	if board.Latest[0].CommandID != "CMD-1" || board.Latest[0].Lifecycle != string(CommandCompleted) {
		t.Fatalf("expected latest CMD-1 state to be completed: %+v", board.Latest)
	}
}

func TestDeriveOperationalPosture(t *testing.T) {
	health := MissionHealthSummary{HealthStatus: "NOMINAL", Readiness: "READY"}
	freshness := MissionFreshnessSummary{FreshnessStatus: "FRESH", ReadinessImpact: "READY"}
	if got := deriveOperationalPosture(health, freshness, &MissionStateConsistency{Status: "IN_SYNC"}); got != "READY" {
		t.Fatalf("expected READY, got %s", got)
	}
	health.HealthStatus = "CRITICAL"
	if got := deriveOperationalPosture(health, freshness, &MissionStateConsistency{Status: "IN_SYNC"}); got != "CRITICAL" {
		t.Fatalf("expected CRITICAL, got %s", got)
	}
	health.HealthStatus = "NOMINAL"
	freshness.FreshnessStatus = "STALE"
	if got := deriveOperationalPosture(health, freshness, &MissionStateConsistency{Status: "IN_SYNC"}); got != "DEGRADED" {
		t.Fatalf("expected DEGRADED, got %s", got)
	}
	freshness.FreshnessStatus = "FRESH"
	if got := deriveOperationalPosture(health, freshness, &MissionStateConsistency{Status: "MISMATCH"}); got != "DESYNCHRONIZED" {
		t.Fatalf("expected DESYNCHRONIZED, got %s", got)
	}
}
