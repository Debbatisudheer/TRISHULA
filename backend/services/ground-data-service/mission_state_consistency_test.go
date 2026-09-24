package main

import (
	"context"
	"testing"
	"time"
)

type consistencyRepo struct {
	records []GroundRecord
}

func (r consistencyRepo) Append(GroundRecord) error { return nil }
func (r consistencyRepo) LoadAll() ([]GroundRecord, error) {
	return append([]GroundRecord(nil), r.records...), nil
}
func (r consistencyRepo) Close() error { return nil }
func (r consistencyRepo) GetByID(_ context.Context, recordID string) (GroundRecord, bool, error) {
	for _, record := range r.records {
		if record.Envelope.RecordID == recordID {
			return record, true, nil
		}
	}
	return GroundRecord{}, false, nil
}
func (r consistencyRepo) Query(_ context.Context, query RecordQuery) ([]GroundRecord, error) {
	out := make([]GroundRecord, 0)
	for _, record := range r.records {
		if record.Envelope.MissionID != query.MissionID {
			continue
		}
		out = append(out, record)
	}
	return out, nil
}

func TestMissionStateConsistencyInSync(t *testing.T) {
	engine := NewMissionStateEngine()
	result := ProcessingResult{MissionID: "TRISHULA", SourceNode: "ROVER-01", SequenceNumber: 3, Kind: KindTelemetry, ProcessedAt: time.Unix(10, 0).UTC(), MissionTimestampNS: 3, Attributes: map[string]string{"metric": "battery", "value": "90", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL"}}
	if err := engine.Apply(result); err != nil {
		t.Fatal(err)
	}
	repo := consistencyRepo{records: []GroundRecord{testConsistencyRecord("R-3", 3)}}
	got, found, err := engine.CheckConsistency(context.Background(), repo, "TRISHULA", time.Unix(20, 0))
	if err != nil {
		t.Fatal(err)
	}
	if !found || got.Status != "IN_SYNC" || got.ReconciliationRequired {
		t.Fatalf("unexpected consistency: %+v", got)
	}
}

func TestMissionStateConsistencyDetectsDivergence(t *testing.T) {
	engine := NewMissionStateEngine()
	result := ProcessingResult{MissionID: "TRISHULA", SourceNode: "ROVER-01", SequenceNumber: 2, Kind: KindTelemetry, ProcessedAt: time.Unix(10, 0).UTC(), MissionTimestampNS: 2, Attributes: map[string]string{"metric": "battery", "value": "90", "quality_class": "GOOD"}}
	if err := engine.Apply(result); err != nil {
		t.Fatal(err)
	}
	repo := consistencyRepo{records: []GroundRecord{testConsistencyRecord("R-3", 3)}}
	got, found, err := engine.CheckConsistency(context.Background(), repo, "TRISHULA", time.Unix(20, 0))
	if err != nil {
		t.Fatal(err)
	}
	if !found || got.Status != "DIVERGED" || !got.ReconciliationRequired {
		t.Fatalf("unexpected divergence: %+v", got)
	}
	if len(got.SequenceMismatches) != 1 || got.SequenceMismatches[0].DurableSequence != 3 || got.SequenceMismatches[0].StateSequence != 2 {
		t.Fatalf("unexpected mismatch: %+v", got.SequenceMismatches)
	}
}

func TestMissionStateConsistencyDetectsMissingState(t *testing.T) {
	engine := NewMissionStateEngine()
	repo := consistencyRepo{records: []GroundRecord{testConsistencyRecord("R-4", 4)}}
	got, found, err := engine.CheckConsistency(context.Background(), repo, "TRISHULA", time.Unix(20, 0))
	if err != nil {
		t.Fatal(err)
	}
	if !found || got.Status != "DIVERGED" || len(got.MissingInState) != 1 || got.MissingInState[0] != "ROVER-01" {
		t.Fatalf("unexpected missing-state result: %+v", got)
	}
}

func testConsistencyRecord(id string, seq uint64) GroundRecord {
	return GroundRecord{Envelope: GroundEnvelope{SchemaVersion: 1, Kind: KindTelemetry, RecordID: id, MissionID: "TRISHULA", SourceNode: "ROVER-01", OriginNode: "ROVER-01", DestinationNode: "GS-TRISHULA-01", Priority: "normal", ApplicationID: 103, MissionTimestamp: seq, SequenceNumber: seq, Quality: 0.99, PayloadSchema: "telemetry.v1"}, Fields: map[string]string{"metric": "battery", "value": "90"}}
}
