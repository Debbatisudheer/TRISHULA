package main

import (
	"context"
	"fmt"
	"sort"
)

// RecordQuery defines the supported operational query filters for ground data.
// Zero values mean the filter is not applied.
type RecordQuery struct {
	MissionID           string
	SourceNode          string
	Kind                DataKind
	CorrelationID       string
	MinMissionTimeNS    *uint64
	MaxMissionTimeNS    *uint64
	MinSequence         *uint64
	MaxSequence         *uint64
	Limit               int
	CursorMissionTimeNS *uint64
	CursorSequence      *uint64
	CursorRecordID      string
}

// RecordRepository extends durable storage with operational retrieval.
type RecordRepository interface {
	RecordStore
	GetByID(ctx context.Context, recordID string) (GroundRecord, bool, error)
	Query(ctx context.Context, query RecordQuery) ([]GroundRecord, error)
}

func validateQuery(query RecordQuery) error {
	if query.MinMissionTimeNS != nil && query.MaxMissionTimeNS != nil && *query.MinMissionTimeNS > *query.MaxMissionTimeNS {
		return fmt.Errorf("min_mission_time_ns cannot exceed max_mission_time_ns")
	}
	if query.MinSequence != nil && query.MaxSequence != nil && *query.MinSequence > *query.MaxSequence {
		return fmt.Errorf("min_sequence cannot exceed max_sequence")
	}
	if query.CursorMissionTimeNS != nil && query.CursorSequence == nil {
		return fmt.Errorf("cursor sequence is required when cursor mission time is set")
	}
	if query.CursorSequence != nil && query.CursorMissionTimeNS == nil {
		return fmt.Errorf("cursor mission time is required when cursor sequence is set")
	}
	if (query.CursorMissionTimeNS != nil || query.CursorSequence != nil) && query.CursorRecordID == "" {
		return fmt.Errorf("cursor record id is required")
	}
	if query.Limit < 0 {
		return fmt.Errorf("limit cannot be negative")
	}
	return nil
}

func recordMatches(record GroundRecord, query RecordQuery) bool {
	e := record.Envelope
	if query.MissionID != "" && e.MissionID != query.MissionID {
		return false
	}
	if query.SourceNode != "" && e.SourceNode != query.SourceNode {
		return false
	}
	if query.Kind != "" && e.Kind != query.Kind {
		return false
	}
	if query.CorrelationID != "" && e.CorrelationID != query.CorrelationID {
		return false
	}
	if query.MinMissionTimeNS != nil && e.MissionTimestamp < *query.MinMissionTimeNS {
		return false
	}
	if query.MaxMissionTimeNS != nil && e.MissionTimestamp > *query.MaxMissionTimeNS {
		return false
	}
	if query.MinSequence != nil && e.SequenceNumber < *query.MinSequence {
		return false
	}
	if query.MaxSequence != nil && e.SequenceNumber > *query.MaxSequence {
		return false
	}
	if query.CursorMissionTimeNS != nil && query.CursorSequence != nil {
		if e.MissionTimestamp > *query.CursorMissionTimeNS {
			return false
		}
		if e.MissionTimestamp == *query.CursorMissionTimeNS && e.SequenceNumber > *query.CursorSequence {
			return false
		}
		if e.MissionTimestamp == *query.CursorMissionTimeNS && e.SequenceNumber == *query.CursorSequence && e.RecordID <= query.CursorRecordID {
			return false
		}
	}
	return true
}

const currentRecordQueryVersion = "v0.9.109.1"

type recordQueryTopKHeap []GroundRecord

// less reports whether a is worse than b. The heap root is therefore always
// the worst retained record and is the first candidate to replace.
func recordQueryTopKWorse(a, b GroundRecord) bool {
	x, y := a.Envelope, b.Envelope
	if x.MissionTimestamp != y.MissionTimestamp {
		return x.MissionTimestamp < y.MissionTimestamp
	}
	if x.SequenceNumber != y.SequenceNumber {
		return x.SequenceNumber < y.SequenceNumber
	}
	return x.RecordID > y.RecordID
}

func (h *recordQueryTopKHeap) push(record GroundRecord) {
	*h = append(*h, record)
	index := len(*h) - 1
	for index > 0 {
		parent := (index - 1) / 2
		if !recordQueryTopKWorse((*h)[index], (*h)[parent]) {
			break
		}
		(*h)[parent], (*h)[index] = (*h)[index], (*h)[parent]
		index = parent
	}
}

func (h *recordQueryTopKHeap) replaceRoot(record GroundRecord) {
	(*h)[0] = record
	index := 0
	for {
		left := index*2 + 1
		if left >= len(*h) {
			return
		}
		right := left + 1
		worstChild := left
		if right < len(*h) && recordQueryTopKWorse((*h)[right], (*h)[left]) {
			worstChild = right
		}
		if !recordQueryTopKWorse((*h)[worstChild], (*h)[index]) {
			return
		}
		(*h)[index], (*h)[worstChild] = (*h)[worstChild], (*h)[index]
		index = worstChild
	}
}

func recordQueryResultIsBetter(a, b GroundRecord) bool {
	x, y := a.Envelope, b.Envelope
	if x.MissionTimestamp != y.MissionTimestamp {
		return x.MissionTimestamp > y.MissionTimestamp
	}
	if x.SequenceNumber != y.SequenceNumber {
		return x.SequenceNumber > y.SequenceNumber
	}
	return x.RecordID < y.RecordID
}

// applyQueryTopK retains only the best query.Limit matching records. This is
// useful for the operational APIs, where callers commonly request a small
// newest-first page from a much larger durable history. The heap keeps the
// worst retained item at its root, so each replacement costs O(log k) while
// memory remains O(k).
func applyQueryTopK(records []GroundRecord, query RecordQuery) []GroundRecord {
	k := query.Limit
	if k <= 0 || len(records) <= k {
		out := make([]GroundRecord, 0, len(records))
		for _, record := range records {
			if recordMatches(record, query) {
				out = append(out, record)
			}
		}
		sortQueryResults(out)
		if k > 0 && len(out) > k {
			out = out[:k]
		}
		return out
	}

	h := make(recordQueryTopKHeap, 0, k)
	for _, record := range records {
		if !recordMatches(record, query) {
			continue
		}
		if len(h) < k {
			h.push(record)
			continue
		}
		if recordQueryResultIsBetter(record, h[0]) {
			h.replaceRoot(record)
		}
	}

	out := make([]GroundRecord, len(h))
	copy(out, h)
	sortQueryResults(out)
	return out
}

func sortQueryResults(records []GroundRecord) {
	sort.Slice(records, func(i, j int) bool {
		a, b := records[i].Envelope, records[j].Envelope
		if a.MissionTimestamp != b.MissionTimestamp {
			return a.MissionTimestamp > b.MissionTimestamp
		}
		if a.SequenceNumber != b.SequenceNumber {
			return a.SequenceNumber > b.SequenceNumber
		}
		return a.RecordID < b.RecordID
	})
}

func applyQuery(records []GroundRecord, query RecordQuery) ([]GroundRecord, error) {
	if err := validateQuery(query); err != nil {
		return nil, err
	}
	if query.Limit > 0 && len(records) > query.Limit {
		return applyQueryTopK(records, query), nil
	}
	out := make([]GroundRecord, 0, len(records))
	for _, record := range records {
		if recordMatches(record, query) {
			out = append(out, record)
		}
	}
	sortQueryResults(out)
	return out, nil
}
