package main

import "testing"

func queryTopKRecord(id string, missionTime, sequence uint64) GroundRecord {
	return GroundRecord{Envelope: GroundEnvelope{
		RecordID:         id,
		MissionID:        "TRISHULA",
		MissionTimestamp: missionTime,
		SequenceNumber:   sequence,
		Kind:             KindTelemetry,
	}}
}

func TestApplyQueryTopKMatchesFullSortOrdering(t *testing.T) {
	records := []GroundRecord{
		queryTopKRecord("R-001", 100, 1),
		queryTopKRecord("R-002", 500, 5),
		queryTopKRecord("R-003", 300, 3),
		queryTopKRecord("R-004", 500, 4),
		queryTopKRecord("R-005", 500, 5),
		queryTopKRecord("R-006", 200, 2),
	}
	query := RecordQuery{MissionID: "TRISHULA", Limit: 3}

	got, err := applyQuery(records, query)
	if err != nil {
		t.Fatalf("applyQuery: %v", err)
	}
	want := []string{"R-002", "R-005", "R-004"}
	if len(got) != len(want) {
		t.Fatalf("got %d records, want %d", len(got), len(want))
	}
	for i, record := range got {
		if record.Envelope.RecordID != want[i] {
			t.Fatalf("result[%d]=%s, want %s", i, record.Envelope.RecordID, want[i])
		}
	}
}

func TestApplyQueryTopKUsesDeterministicRecordIDTieBreak(t *testing.T) {
	records := []GroundRecord{
		queryTopKRecord("R-Z", 100, 10),
		queryTopKRecord("R-A", 100, 10),
		queryTopKRecord("R-M", 100, 10),
	}
	query := RecordQuery{MissionID: "TRISHULA", Limit: 2}

	got, err := applyQuery(records, query)
	if err != nil {
		t.Fatalf("applyQuery: %v", err)
	}
	want := []string{"R-A", "R-M"}
	for i, record := range got {
		if record.Envelope.RecordID != want[i] {
			t.Fatalf("result[%d]=%s, want %s", i, record.Envelope.RecordID, want[i])
		}
	}
}

func TestApplyQueryTopKPreservesFilters(t *testing.T) {
	records := []GroundRecord{
		queryTopKRecord("A-1", 500, 5),
		queryTopKRecord("B-2", 900, 9),
		queryTopKRecord("A-3", 700, 7),
		queryTopKRecord("A-4", 600, 6),
	}
	records[1].Envelope.SourceNode = "OTHER"
	records[2].Envelope.SourceNode = "ROVER-01"
	records[0].Envelope.SourceNode = "ROVER-01"
	records[3].Envelope.SourceNode = "ROVER-01"

	query := RecordQuery{MissionID: "TRISHULA", SourceNode: "ROVER-01", Limit: 2}
	got, err := applyQuery(records, query)
	if err != nil {
		t.Fatalf("applyQuery: %v", err)
	}
	want := []string{"A-3", "A-4"}
	for i, record := range got {
		if record.Envelope.RecordID != want[i] {
			t.Fatalf("result[%d]=%s, want %s", i, record.Envelope.RecordID, want[i])
		}
	}
}

func TestApplyQueryTopKZeroLimitKeepsFullResultBehavior(t *testing.T) {
	records := []GroundRecord{
		queryTopKRecord("R-1", 100, 1),
		queryTopKRecord("R-2", 300, 3),
		queryTopKRecord("R-3", 200, 2),
	}
	got, err := applyQuery(records, RecordQuery{MissionID: "TRISHULA"})
	if err != nil {
		t.Fatalf("applyQuery: %v", err)
	}
	want := []string{"R-2", "R-3", "R-1"}
	if len(got) != len(want) {
		t.Fatalf("got %d records, want %d", len(got), len(want))
	}
	for i, record := range got {
		if record.Envelope.RecordID != want[i] {
			t.Fatalf("result[%d]=%s, want %s", i, record.Envelope.RecordID, want[i])
		}
	}
}
