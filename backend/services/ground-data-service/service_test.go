package main

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func sampleRecord(id string, seq uint64, kind DataKind) GroundRecord {
	return GroundRecord{Envelope: GroundEnvelope{
		SchemaVersion: 1, Kind: kind, RecordID: id, MissionID: "TRISHULA",
		SourceNode: "ROVER-01", OriginNode: "ROVER-01", DestinationNode: "GS-TRISHULA-01",
		Priority: "normal", ApplicationID: 103, MissionTimestamp: 1_000_000,
		SequenceNumber: seq, Quality: 0.98, PayloadSchema: string(kind) + ".v1",
	}, Fields: map[string]string{"metric": "battery", "value": "91.2"}}
}

func TestValidation(t *testing.T) {
	if err := validateRecord(sampleRecord("TRS-1", 1, KindTelemetry)); err != nil {
		t.Fatalf("expected valid record, got %v", err)
	}
	invalid := sampleRecord("", 1, KindTelemetry)
	if err := validateRecord(invalid); err == nil {
		t.Fatal("expected invalid record_id to fail")
	}
}

func TestIngestDuplicateAndOutOfOrder(t *testing.T) {
	svc := NewService(10)
	first := svc.Ingest(sampleRecord("A", 10, KindTelemetry))
	if !first.Accepted || first.Duplicate || first.OutOfOrder {
		t.Fatalf("bad first result: %+v", first)
	}
	duplicate := svc.Ingest(sampleRecord("A", 10, KindTelemetry))
	if !duplicate.Duplicate || duplicate.Accepted {
		t.Fatalf("bad duplicate result: %+v", duplicate)
	}
	older := svc.Ingest(sampleRecord("B", 9, KindTelemetry))
	if !older.Accepted || !older.OutOfOrder {
		t.Fatalf("expected out-of-order acceptance: %+v", older)
	}
}

func TestHTTPBoundary(t *testing.T) {
	svc := NewService(10)
	mux := http.NewServeMux()
	mux.HandleFunc("POST /v1/ingest", func(w http.ResponseWriter, r *http.Request) {
		defer r.Body.Close()
		var record GroundRecord
		decoder := json.NewDecoder(r.Body)
		decoder.DisallowUnknownFields()
		if err := decoder.Decode(&record); err != nil {
			http.Error(w, err.Error(), 400)
			return
		}
		if err := validateRecord(record); err != nil {
			http.Error(w, err.Error(), 422)
			return
		}
		writeJSON(w, http.StatusAccepted, svc.Ingest(record))
	})
	payload, _ := json.Marshal(sampleRecord("HTTP-1", 1, KindScience))
	req := httptest.NewRequest(http.MethodPost, "/v1/ingest", strings.NewReader(string(payload)))
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, req)
	if rec.Code != http.StatusAccepted {
		t.Fatalf("expected 202, got %d: %s", rec.Code, rec.Body.String())
	}
}

func TestPersistentReload(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "records.jsonl")

	store, err := NewJSONLStore(path)
	if err != nil {
		t.Fatalf("create store: %v", err)
	}
	svc := NewServiceWithStore(10, store)
	result := svc.Ingest(sampleRecord("PERSIST-1", 7, KindTelemetry))
	if !result.Accepted {
		t.Fatalf("expected accepted record: %+v", result)
	}
	if err := svc.Close(); err != nil {
		t.Fatalf("close first service: %v", err)
	}

	reloadedStore, err := NewJSONLStore(path)
	if err != nil {
		t.Fatalf("reopen store: %v", err)
	}
	reloaded := NewServiceWithStore(10, reloadedStore)
	defer reloaded.Close()

	state := reloaded.Snapshot()
	if state.LastSequence["ROVER-01"] != 7 {
		t.Fatalf("expected restored sequence 7, got %d", state.LastSequence["ROVER-01"])
	}
	if state.Counters[KindTelemetry] != 1 {
		t.Fatalf("expected restored telemetry counter 1, got %d", state.Counters[KindTelemetry])
	}
	duplicate := reloaded.Ingest(sampleRecord("PERSIST-1", 7, KindTelemetry))
	if !duplicate.Duplicate || duplicate.Accepted {
		t.Fatalf("expected duplicate after restart: %+v", duplicate)
	}
}

func TestArchiveSurvivesRestart(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "records.jsonl")
	store, err := NewJSONLStore(path)
	if err != nil {
		t.Fatalf("create store: %v", err)
	}
	svc := NewServiceWithStore(10, store)
	_ = svc.Ingest(sampleRecord("ARCHIVE-1", 1, KindScience))
	_ = svc.Ingest(sampleRecord("ARCHIVE-2", 2, KindEvent))
	_ = svc.Close()

	info, err := os.Stat(path)
	if err != nil {
		t.Fatalf("stat archive: %v", err)
	}
	if info.Size() <= 0 {
		t.Fatal("expected non-empty persistent archive")
	}
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatalf("read archive: %v", err)
	}
	if !strings.Contains(string(raw), "ARCHIVE-1") || !strings.Contains(string(raw), "ARCHIVE-2") {
		t.Fatalf("archive does not contain expected record IDs: %s", string(raw))
	}
}

func TestRepositoryQueries(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "records.jsonl")
	store, err := NewJSONLStore(path)
	if err != nil {
		t.Fatalf("create store: %v", err)
	}
	defer store.Close()

	records := []GroundRecord{
		sampleRecord("Q-001", 1, KindTelemetry),
		sampleRecord("Q-002", 2, KindTelemetry),
		sampleRecord("Q-003", 3, KindScience),
	}
	records[0].Envelope.MissionTimestamp = 100
	records[1].Envelope.MissionTimestamp = 200
	records[2].Envelope.MissionTimestamp = 300
	records[2].Envelope.CorrelationID = "CORR-3"
	for _, record := range records {
		if err := store.Append(record); err != nil {
			t.Fatalf("append %s: %v", record.Envelope.RecordID, err)
		}
	}

	ctx := context.Background()
	got, found, err := store.GetByID(ctx, "Q-002")
	if err != nil || !found || got.Envelope.SequenceNumber != 2 {
		t.Fatalf("get by id mismatch: found=%v err=%v record=%+v", found, err, got)
	}
	_, found, err = store.GetByID(ctx, "missing")
	if err != nil || found {
		t.Fatalf("expected missing record: found=%v err=%v", found, err)
	}

	kind := KindTelemetry
	results, err := store.Query(ctx, RecordQuery{Kind: kind, Limit: 10})
	if err != nil {
		t.Fatalf("query by kind: %v", err)
	}
	if len(results) != 2 || results[0].Envelope.RecordID != "Q-002" {
		t.Fatalf("expected newest telemetry first, got %+v", results)
	}

	minSeq := uint64(2)
	maxSeq := uint64(2)
	results, err = store.Query(ctx, RecordQuery{SourceNode: "ROVER-01", MinSequence: &minSeq, MaxSequence: &maxSeq})
	if err != nil || len(results) != 1 || results[0].Envelope.RecordID != "Q-002" {
		t.Fatalf("sequence query mismatch: err=%v results=%+v", err, results)
	}

	results, err = store.Query(ctx, RecordQuery{CorrelationID: "CORR-3"})
	if err != nil || len(results) != 1 || results[0].Envelope.RecordID != "Q-003" {
		t.Fatalf("correlation query mismatch: err=%v results=%+v", err, results)
	}
}

func TestRepositoryQueryValidation(t *testing.T) {
	min := uint64(10)
	max := uint64(5)
	if _, err := applyQuery(nil, RecordQuery{MinSequence: &min, MaxSequence: &max}); err == nil {
		t.Fatal("expected invalid sequence range")
	}
	if _, err := applyQuery(nil, RecordQuery{Limit: -1}); err == nil {
		t.Fatal("expected negative limit to fail")
	}
}

func TestQueryHTTPParameterParsing(t *testing.T) {
	req := httptest.NewRequest(http.MethodGet, "/v1/records?kind=telemetry&source_node=ROVER-01&min_sequence=2&max_sequence=5&limit=10", nil)
	query, err := parseRecordQuery(req)
	if err != nil {
		t.Fatalf("parse query: %v", err)
	}
	if query.Kind != KindTelemetry || query.SourceNode != "ROVER-01" || *query.MinSequence != 2 || *query.MaxSequence != 5 || query.Limit != 10 {
		t.Fatalf("unexpected query: %+v", query)
	}
}

func TestQueryHTTPParameterValidation(t *testing.T) {
	req := httptest.NewRequest(http.MethodGet, "/v1/records?kind=unknown", nil)
	if _, err := parseRecordQuery(req); err == nil {
		t.Fatal("expected unsupported kind to fail")
	}
	req = httptest.NewRequest(http.MethodGet, "/v1/records?min_sequence=9&max_sequence=2", nil)
	if _, err := parseRecordQuery(req); err == nil {
		t.Fatal("expected invalid sequence range to fail")
	}
}
