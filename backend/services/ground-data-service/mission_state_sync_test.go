package main

import (
	"context"
	"testing"
	"time"
)

type syncRepository struct {
	records []GroundRecord
}

func (r syncRepository) Append(GroundRecord) error { return nil }
func (r syncRepository) LoadAll() ([]GroundRecord, error) {
	return append([]GroundRecord(nil), r.records...), nil
}
func (r syncRepository) Close() error { return nil }
func (r syncRepository) GetByID(_ context.Context, recordID string) (GroundRecord, bool, error) {
	for _, record := range r.records {
		if record.Envelope.RecordID == recordID {
			return record, true, nil
		}
	}
	return GroundRecord{}, false, nil
}
func (r syncRepository) Query(_ context.Context, query RecordQuery) ([]GroundRecord, error) {
	return applyQuery(r.records, query)
}

func syncTelemetryRecord(id string, seq uint64, metric, value string) GroundRecord {
	record := GroundRecord{
		Envelope: GroundEnvelope{
			SchemaVersion:    1,
			Kind:             KindTelemetry,
			RecordID:         id,
			MissionID:        "TRISHULA",
			SourceNode:       "ROVER-01",
			OriginNode:       "ROVER-01",
			DestinationNode:  "GS-TRISHULA-01",
			Priority:         "normal",
			ApplicationID:    103,
			MissionTimestamp: seq,
			SequenceNumber:   seq,
			Quality:          0.99,
			PayloadSchema:    "telemetry.v1",
		},
		Fields: map[string]string{
			"metric": metric,
			"value":  value,
		},
	}
	// Use a metric-compatible unit in the synchronization fixture so replay
	// exercises the same telemetry validation rules as production records.
	switch metric {
	case "battery":
		record.Fields["unit"] = "%"
	case "temperature":
		record.Fields["unit"] = "K"
	}
	return record
}

func TestMissionStateSynchronizationRebuildsFromDurableHistory(t *testing.T) {
	router := NewRecordRouter()
	_ = router.Route(syncTelemetryRecord("LIVE-OLD", 2, "battery", "80"))

	repo := syncRepository{records: []GroundRecord{
		syncTelemetryRecord("R-3", 3, "battery", "90"),
		syncTelemetryRecord("R-5", 5, "temperature", "300"),
	}}

	result, found, err := router.SynchronizeMissionState(context.Background(), repo, "TRISHULA", time.Unix(30, 0))
	if err != nil {
		t.Fatal(err)
	}
	if !found || result.Status != "SYNCHRONIZED" {
		t.Fatalf("unexpected sync result: %+v", result)
	}
	if result.PreviousLatestSequenceBySource["ROVER-01"] != 2 {
		t.Fatalf("expected previous sequence 2, got %+v", result.PreviousLatestSequenceBySource)
	}
	if result.SynchronizedLatestSequenceBySource["ROVER-01"] != 5 {
		t.Fatalf("expected synchronized sequence 5, got %+v", result.SynchronizedLatestSequenceBySource)
	}
	mission, ok := router.Mission("TRISHULA")
	if !ok || mission.Vehicles["ROVER-01"].LastSequence != 5 {
		t.Fatalf("unexpected rebuilt mission: %+v", mission)
	}
}

func TestMissionStateSynchronizationLeavesLiveStateUntouchedOnReplayFailure(t *testing.T) {
	router := NewRecordRouter()
	_ = router.Route(syncTelemetryRecord("LIVE-1", 1, "battery", "90"))

	bad := syncTelemetryRecord("BAD-2", 2, "battery", "not-a-number")
	repo := syncRepository{records: []GroundRecord{bad}}

	result, found, err := router.SynchronizeMissionState(context.Background(), repo, "TRISHULA", time.Unix(30, 0))
	if err == nil || found || result.Status != "FAILED" {
		t.Fatalf("expected failed synchronization: result=%+v found=%v err=%v", result, found, err)
	}
	mission, ok := router.Mission("TRISHULA")
	if !ok || mission.Vehicles["ROVER-01"].LastSequence != 1 {
		t.Fatalf("live state changed after failed sync: %+v", mission)
	}
}

func TestMissionStateSynchronizationRestoresCommandSnapshotWithoutLiveLifecycleReplay(t *testing.T) {
	router := NewRecordRouter()
	record := sampleRecord("CMD-COMPLETE", 7, KindCommand)
	record.Envelope.MissionID = "TRISHULA"
	record.Fields = map[string]string{
		"command_id": "CMD-1",
		"command":    "STOP_ROVER",
		"target":     "ROVER-01",
		"lifecycle":  "COMPLETED",
	}
	repo := syncRepository{records: []GroundRecord{record}}

	result, found, err := router.SynchronizeMissionState(context.Background(), repo, "TRISHULA", time.Unix(30, 0))
	if err != nil {
		t.Fatal(err)
	}
	if !found || result.Status != "SYNCHRONIZED" {
		t.Fatalf("unexpected sync result: %+v", result)
	}
	mission, ok := router.Mission("TRISHULA")
	if !ok {
		t.Fatal("expected synchronized mission")
	}
	command := mission.Vehicles["ROVER-01"].LastCommand
	if command == nil || command.CommandID != "CMD-1" || command.Lifecycle != string(CommandCompleted) || !command.Terminal {
		t.Fatalf("unexpected restored command snapshot: %+v", command)
	}
}

func TestMissionStateSynchronizationNoData(t *testing.T) {
	router := NewRecordRouter()
	repo := syncRepository{records: nil}
	result, found, err := router.SynchronizeMissionState(context.Background(), repo, "TRISHULA", time.Unix(30, 0))
	if err != nil {
		t.Fatal(err)
	}
	if found || result.Status != "NO_DATA" {
		t.Fatalf("unexpected no-data result: %+v found=%v", result, found)
	}
}

func TestMissionStateSynchronizationIsDeterministicAcrossRepositoryOrder(t *testing.T) {
	routerA := NewRecordRouter()
	routerB := NewRecordRouter()
	records := []GroundRecord{
		syncTelemetryRecord("R-5", 5, "temperature", "300"),
		syncTelemetryRecord("R-3", 3, "battery", "90"),
		syncTelemetryRecord("R-4", 4, "battery", "88"),
	}

	_, foundA, err := routerA.SynchronizeMissionState(context.Background(), syncRepository{records: records}, "TRISHULA", time.Unix(30, 0))
	if err != nil || !foundA {
		t.Fatalf("sync A failed: found=%v err=%v", foundA, err)
	}
	reversed := append([]GroundRecord(nil), records...)
	for i, j := 0, len(reversed)-1; i < j; i, j = i+1, j-1 {
		reversed[i], reversed[j] = reversed[j], reversed[i]
	}
	_, foundB, err := routerB.SynchronizeMissionState(context.Background(), syncRepository{records: reversed}, "TRISHULA", time.Unix(30, 0))
	if err != nil || !foundB {
		t.Fatalf("sync B failed: found=%v err=%v", foundB, err)
	}
	missionA, _ := routerA.Mission("TRISHULA")
	missionB, _ := routerB.Mission("TRISHULA")
	if missionA.Vehicles["ROVER-01"].LastSequence != missionB.Vehicles["ROVER-01"].LastSequence {
		t.Fatalf("non-deterministic vehicle sequence: A=%d B=%d", missionA.Vehicles["ROVER-01"].LastSequence, missionB.Vehicles["ROVER-01"].LastSequence)
	}
}
