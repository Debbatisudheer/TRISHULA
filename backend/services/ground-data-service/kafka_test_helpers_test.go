package main

import "context"

type memoryRecordStore struct{ records []GroundRecord }

func newMemoryRecordStore() *memoryRecordStore { return &memoryRecordStore{} }
func (m *memoryRecordStore) Append(r GroundRecord) error {
	m.records = append(m.records, r)
	return nil
}
func (m *memoryRecordStore) LoadAll() ([]GroundRecord, error) {
	return append([]GroundRecord(nil), m.records...), nil
}
func (m *memoryRecordStore) Close() error { return nil }
func testGroundRecord(id string, kind DataKind, seq uint64) GroundRecord {
	return GroundRecord{Envelope: GroundEnvelope{SchemaVersion: 1, Kind: kind, RecordID: id, MissionID: "TRISHULA", SourceNode: "ROVER-01", OriginNode: "ROVER-01", DestinationNode: "GS-TRISHULA-01", Priority: "normal", ApplicationID: 103, MissionTimestamp: seq * 1000, SequenceNumber: seq, Quality: 0.99, PayloadSchema: "telemetry.v1"}, Fields: map[string]string{"battery": "90.0"}}
}

var _ RecordStore = (*memoryRecordStore)(nil)
var _ = context.Background
