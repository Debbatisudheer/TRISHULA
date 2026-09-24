package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sync"
	"time"
)

type DataKind string

const (
	KindTelemetry DataKind = "telemetry"
	KindScience   DataKind = "science"
	KindEvent     DataKind = "event"
	KindCommand   DataKind = "command"
	KindFile      DataKind = "file"
)

type GroundEnvelope struct {
	SchemaVersion    uint16   `json:"schema_version"`
	Kind             DataKind `json:"kind"`
	RecordID         string   `json:"record_id"`
	MissionID        string   `json:"mission_id"`
	SourceNode       string   `json:"source_node"`
	OriginNode       string   `json:"origin_node"`
	DestinationNode  string   `json:"destination_node"`
	Priority         string   `json:"priority"`
	ApplicationID    uint16   `json:"application_id"`
	MissionTimestamp uint64   `json:"mission_timestamp_ns"`
	SequenceNumber   uint64   `json:"sequence_number"`
	Quality          float64  `json:"quality"`
	CorrelationID    string   `json:"correlation_id,omitempty"`
	PayloadSchema    string   `json:"payload_schema"`
}

type GroundRecord struct {
	Envelope GroundEnvelope    `json:"envelope"`
	Fields   map[string]string `json:"fields"`
}

type IngestResult struct {
	Accepted        bool   `json:"accepted"`
	Duplicate       bool   `json:"duplicate"`
	OutOfOrder      bool   `json:"out_of_order"`
	StreamPublished bool   `json:"stream_published"`
	StreamError     string `json:"stream_error,omitempty"`
	Reason          string `json:"reason,omitempty"`
	CurrentKind     string `json:"current_kind,omitempty"`
	ReceivedCount   uint64 `json:"received_count"`
}

type CurrentState struct {
	LatestByKind map[DataKind]GroundRecord `json:"latest_by_kind"`
	LastSequence map[string]uint64         `json:"last_sequence_by_source"`
	Counters     map[DataKind]uint64       `json:"counters"`
}

// RecordStore is the persistence boundary for accepted ground records.
// Keeping this interface small lets us use the local durable store now and
// keep durable storage separate from the current-state cache without changing ingest logic.
type RecordStore interface {
	Append(GroundRecord) error
	LoadAll() ([]GroundRecord, error)
	Close() error
}

// JSONLStore is an append-only durable local ground-data archive.
// Each accepted record is one JSON line. It is intentionally dependency-free
// so TRISHULA can run locally without requiring a database server.
type JSONLStore struct {
	mu   sync.Mutex
	path string
	file *os.File
}

func NewJSONLStore(path string) (*JSONLStore, error) {
	if path == "" {
		return nil, errors.New("storage path is required")
	}
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return nil, fmt.Errorf("create storage directory: %w", err)
	}
	file, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		return nil, fmt.Errorf("open storage file: %w", err)
	}
	return &JSONLStore{path: path, file: file}, nil
}

func (s *JSONLStore) Append(record GroundRecord) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	data, err := json.Marshal(record)
	if err != nil {
		return fmt.Errorf("encode persistent record: %w", err)
	}
	data = append(data, '\n')
	if _, err := s.file.Write(data); err != nil {
		return fmt.Errorf("write persistent record: %w", err)
	}
	if err := s.file.Sync(); err != nil {
		return fmt.Errorf("sync persistent record: %w", err)
	}
	return nil
}

func (s *JSONLStore) LoadAll() ([]GroundRecord, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if err := s.file.Sync(); err != nil {
		return nil, fmt.Errorf("sync before load: %w", err)
	}
	file, err := os.Open(s.path)
	if err != nil {
		return nil, fmt.Errorf("open storage archive: %w", err)
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	scanner.Buffer(make([]byte, 0, 64*1024), 8*1024*1024)
	var records []GroundRecord
	line := 0
	for scanner.Scan() {
		line++
		raw := scanner.Bytes()
		var record GroundRecord
		if err := json.Unmarshal(raw, &record); err != nil {
			return nil, fmt.Errorf("decode storage record at line %d: %w", line, err)
		}
		records = append(records, record)
	}
	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("read storage archive: %w", err)
	}
	return records, nil
}

func (s *JSONLStore) GetByID(ctx context.Context, recordID string) (GroundRecord, bool, error) {
	if err := ctx.Err(); err != nil {
		return GroundRecord{}, false, err
	}
	records, err := s.LoadAll()
	if err != nil {
		return GroundRecord{}, false, err
	}
	for _, record := range records {
		if record.Envelope.RecordID == recordID {
			return record, true, nil
		}
	}
	return GroundRecord{}, false, nil
}

func (s *JSONLStore) Query(ctx context.Context, query RecordQuery) ([]GroundRecord, error) {
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	records, err := s.LoadAll()
	if err != nil {
		return nil, err
	}
	return applyQuery(records, query)
}

func (s *JSONLStore) Close() error {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.file == nil {
		return nil
	}
	err := s.file.Close()
	s.file = nil
	return err
}

func (s *Service) LoadRecords() ([]GroundRecord, error) {
	if s == nil || s.store == nil {
		return nil, nil
	}
	return s.store.LoadAll()
}

type StateCache interface {
	SetSnapshot(ctx context.Context, state CurrentState) error
	GetSnapshot(ctx context.Context) (CurrentState, bool, error)
	Close() error
}

type Service struct {
	mu            sync.RWMutex
	seenRecordIDs map[string]struct{}
	lastSequence  map[string]uint64
	latestByKind  map[DataKind]GroundRecord
	counters      map[DataKind]uint64
	received      uint64
	maxRecords    int
	store         RecordStore
	cache         StateCache
	publisher     RecordPublisher
}

func NewService(maxRecords int) *Service {
	return NewServiceWithStore(maxRecords, nil)
}

func NewServiceWithStore(maxRecords int, store RecordStore) *Service {
	return NewServiceWithStoreAndCache(maxRecords, store, nil)
}

func NewServiceWithStoreAndCache(maxRecords int, store RecordStore, cache StateCache) *Service {
	return newService(maxRecords, store, cache, nil)
}

func newService(maxRecords int, store RecordStore, cache StateCache, publisher RecordPublisher) *Service {
	if maxRecords < 1 {
		maxRecords = 10000
	}
	service := &Service{
		seenRecordIDs: make(map[string]struct{}, maxRecords),
		lastSequence:  make(map[string]uint64),
		latestByKind:  make(map[DataKind]GroundRecord),
		counters:      make(map[DataKind]uint64),
		maxRecords:    maxRecords,
		store:         store,
		cache:         cache,
		publisher:     publisher,
	}
	if store != nil {
		records, err := store.LoadAll()
		if err != nil {
			panic(fmt.Sprintf("load persistent ground records: %v", err))
		}
		service.rebuild(records)
	}
	if service.cache != nil {
		_ = service.cache.SetSnapshot(context.Background(), service.snapshotLocked())
	}
	return service
}

func (s *Service) rebuild(records []GroundRecord) {
	for _, record := range records {
		s.received++
		if record.Envelope.RecordID == "" {
			continue
		}
		s.seenRecordIDs[record.Envelope.RecordID] = struct{}{}
		source := record.Envelope.SourceNode
		if previous, ok := s.lastSequence[source]; !ok || record.Envelope.SequenceNumber > previous {
			s.lastSequence[source] = record.Envelope.SequenceNumber
		}
		current, ok := s.latestByKind[record.Envelope.Kind]
		if !ok || record.Envelope.MissionTimestamp > current.Envelope.MissionTimestamp ||
			(record.Envelope.MissionTimestamp == current.Envelope.MissionTimestamp && record.Envelope.SequenceNumber > current.Envelope.SequenceNumber) {
			s.latestByKind[record.Envelope.Kind] = record
		}
		s.counters[record.Envelope.Kind]++
	}
	if len(s.seenRecordIDs) > s.maxRecords {
		s.trimSeenIDs()
	}
}

func (s *Service) trimSeenIDs() {
	keep := make(map[string]struct{}, len(s.latestByKind))
	for _, latest := range s.latestByKind {
		keep[latest.Envelope.RecordID] = struct{}{}
	}
	s.seenRecordIDs = keep
}

func (s *Service) Ingest(record GroundRecord) IngestResult {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.received++
	if _, exists := s.seenRecordIDs[record.Envelope.RecordID]; exists {
		return IngestResult{Accepted: false, Duplicate: true, Reason: "duplicate record_id", CurrentKind: string(record.Envelope.Kind), ReceivedCount: s.received}
	}

	key := record.Envelope.SourceNode
	previous, hasPrevious := s.lastSequence[key]
	outOfOrder := hasPrevious && record.Envelope.SequenceNumber < previous

	if s.store != nil {
		if err := s.store.Append(record); err != nil {
			return IngestResult{Accepted: false, Reason: "persistent storage failure: " + err.Error(), CurrentKind: string(record.Envelope.Kind), ReceivedCount: s.received}
		}
	}

	streamPublished := false
	streamError := ""
	if s.publisher != nil {
		if err := s.publisher.Publish(context.Background(), record); err != nil {
			streamError = err.Error()
		} else {
			streamPublished = true
		}
	}

	s.seenRecordIDs[record.Envelope.RecordID] = struct{}{}
	if len(s.seenRecordIDs) > s.maxRecords {
		s.trimSeenIDs()
		s.seenRecordIDs[record.Envelope.RecordID] = struct{}{}
	}

	if !hasPrevious || record.Envelope.SequenceNumber > previous {
		s.lastSequence[key] = record.Envelope.SequenceNumber
	}

	current, hasCurrent := s.latestByKind[record.Envelope.Kind]
	if !hasCurrent || record.Envelope.MissionTimestamp > current.Envelope.MissionTimestamp ||
		(record.Envelope.MissionTimestamp == current.Envelope.MissionTimestamp && record.Envelope.SequenceNumber > current.Envelope.SequenceNumber) {
		s.latestByKind[record.Envelope.Kind] = record
	}
	s.counters[record.Envelope.Kind]++

	reason := "accepted"
	if outOfOrder {
		reason = "accepted with out-of-order sequence"
	}
	if s.cache != nil {
		_ = s.cache.SetSnapshot(context.Background(), s.snapshotLocked())
	}
	return IngestResult{Accepted: true, OutOfOrder: outOfOrder, StreamPublished: streamPublished, StreamError: streamError, Reason: reason, CurrentKind: string(record.Envelope.Kind), ReceivedCount: s.received}
}

func (s *Service) snapshotLocked() CurrentState {
	latest := make(map[DataKind]GroundRecord, len(s.latestByKind))
	for k, v := range s.latestByKind {
		latest[k] = v
	}
	sequences := make(map[string]uint64, len(s.lastSequence))
	for k, v := range s.lastSequence {
		sequences[k] = v
	}
	counters := make(map[DataKind]uint64, len(s.counters))
	for k, v := range s.counters {
		counters[k] = v
	}
	return CurrentState{LatestByKind: latest, LastSequence: sequences, Counters: counters}
}

func (s *Service) Snapshot() CurrentState {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.snapshotLocked()
}

func (s *Service) Close() error {
	var firstErr error
	if s.cache != nil {
		if err := s.cache.Close(); err != nil {
			firstErr = err
		}
	}
	if s.store != nil {
		if err := s.store.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	return firstErr
}

func validateRecord(record GroundRecord) error {
	e := record.Envelope
	if e.SchemaVersion == 0 || e.SchemaVersion > 1 {
		return errors.New("unsupported schema_version")
	}
	switch e.Kind {
	case KindTelemetry, KindScience, KindEvent, KindCommand, KindFile:
	default:
		return fmt.Errorf("unsupported kind %q", e.Kind)
	}
	if e.RecordID == "" || e.MissionID == "" || e.SourceNode == "" || e.OriginNode == "" || e.DestinationNode == "" {
		return errors.New("record_id, mission_id, source_node, origin_node, and destination_node are required")
	}
	if e.PayloadSchema == "" {
		return errors.New("payload_schema is required")
	}
	if e.Quality < 0 || e.Quality > 1 || e.Quality != e.Quality {
		return errors.New("quality must be within [0,1]")
	}
	if len(record.Fields) == 0 {
		return errors.New("fields are required")
	}
	for key, value := range record.Fields {
		if key == "" {
			return errors.New("field name cannot be empty")
		}
		if len(value) > 4096 {
			return fmt.Errorf("field %q exceeds 4096 bytes", key)
		}
	}
	if e.MissionTimestamp > uint64(time.Now().Add(24*time.Hour).UnixNano()) {
		return errors.New("mission_timestamp_ns is unreasonably far in the future")
	}
	return nil
}

// Ensure the compiler keeps io available for future repository implementations
// while documenting that storage streams are intentionally line-oriented.
var _ = io.EOF
