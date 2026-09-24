package main

import (
	"net/http/httptest"
	"testing"
)

func TestCommandSearchPageTokenRoundTrip(t *testing.T) {
	record := GroundRecord{Envelope: GroundEnvelope{MissionTimestamp: 123, SequenceNumber: 9, RecordID: "CMD-1"}}
	token := encodeCommandPageToken(record)
	decoded, err := decodeCommandPageToken(token)
	if err != nil {
		t.Fatalf("decode token: %v", err)
	}
	if decoded.RecordID != "CMD-1" || decoded.MissionTimestampNS != 123 || decoded.SequenceNumber != 9 {
		t.Fatalf("unexpected token: %+v", decoded)
	}
}

func TestParseCommandSearchQuery(t *testing.T) {
	req := httptest.NewRequest("GET", "/v1/commands/search?mission_id=TRISHULA&page_size=25", nil)
	query, pageSize, err := parseCommandSearchQuery(req)
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	if query.Kind != KindCommand || query.MissionID != "TRISHULA" || pageSize != 25 || query.Limit != 26 {
		t.Fatalf("unexpected query: %+v pageSize=%d", query, pageSize)
	}
}

func TestApplyQueryCursor(t *testing.T) {
	records := []GroundRecord{
		{Envelope: GroundEnvelope{MissionID: "TRISHULA", Kind: KindCommand, MissionTimestamp: 300, SequenceNumber: 3, RecordID: "A"}},
		{Envelope: GroundEnvelope{MissionID: "TRISHULA", Kind: KindCommand, MissionTimestamp: 200, SequenceNumber: 2, RecordID: "B"}},
		{Envelope: GroundEnvelope{MissionID: "TRISHULA", Kind: KindCommand, MissionTimestamp: 100, SequenceNumber: 1, RecordID: "C"}},
	}
	cursorTime, cursorSeq := uint64(200), uint64(2)
	page, err := applyQuery(records, RecordQuery{MissionID: "TRISHULA", Kind: KindCommand, CursorMissionTimeNS: &cursorTime, CursorSequence: &cursorSeq, CursorRecordID: "B", Limit: 10})
	if err != nil {
		t.Fatalf("apply query: %v", err)
	}
	if len(page) != 1 || page[0].Envelope.RecordID != "C" {
		t.Fatalf("unexpected cursor page: %+v", page)
	}
}
