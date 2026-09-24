package main

import (
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"
)

const currentCommandSearchVersion = "v0.9.90"

// CommandSearchView is the stable paginated command-search response.
type CommandSearchView struct {
	RequestID     string         `json:"request_id"`
	APIVersion    string         `json:"api_version"`
	Count         int            `json:"count"`
	PageSize      int            `json:"page_size"`
	HasMore       bool           `json:"has_more"`
	NextPageToken string         `json:"next_page_token,omitempty"`
	GeneratedAt   time.Time      `json:"generated_at"`
	Commands      []GroundRecord `json:"commands"`
}

type commandPageToken struct {
	MissionTimestampNS uint64 `json:"mission_timestamp_ns"`
	SequenceNumber     uint64 `json:"sequence_number"`
	RecordID           string `json:"record_id"`
}

func encodeCommandPageToken(record GroundRecord) string {
	token, _ := json.Marshal(commandPageToken{
		MissionTimestampNS: record.Envelope.MissionTimestamp,
		SequenceNumber:     record.Envelope.SequenceNumber,
		RecordID:           record.Envelope.RecordID,
	})
	return base64.RawURLEncoding.EncodeToString(token)
}

func decodeCommandPageToken(raw string) (commandPageToken, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return commandPageToken{}, nil
	}
	decoded, err := base64.RawURLEncoding.DecodeString(raw)
	if err != nil {
		return commandPageToken{}, fmt.Errorf("invalid page_token")
	}
	var token commandPageToken
	if err := json.Unmarshal(decoded, &token); err != nil || token.RecordID == "" {
		return commandPageToken{}, fmt.Errorf("invalid page_token")
	}
	return token, nil
}

func parseCommandSearchQuery(r *http.Request) (RecordQuery, int, error) {
	if r == nil {
		return RecordQuery{}, 0, fmt.Errorf("request is required")
	}
	values := r.URL.Query()
	query, err := parseRecordQuery(r)
	if err != nil {
		return RecordQuery{}, 0, err
	}
	query.Kind = KindCommand

	pageSize := 50
	if raw := strings.TrimSpace(values.Get("page_size")); raw != "" {
		pageSize, err = strconv.Atoi(raw)
		if err != nil || pageSize < 1 || pageSize > 200 {
			return RecordQuery{}, 0, fmt.Errorf("page_size must be between 1 and 200")
		}
	}
	token, err := decodeCommandPageToken(values.Get("page_token"))
	if err != nil {
		return RecordQuery{}, 0, err
	}
	if token.RecordID != "" {
		query.CursorMissionTimeNS = &token.MissionTimestampNS
		query.CursorSequence = &token.SequenceNumber
		query.CursorRecordID = token.RecordID
	}
	query.Limit = pageSize + 1
	return query, pageSize, nil
}

func buildCommandSearchView(r *http.Request, records []GroundRecord, pageSize int) CommandSearchView {
	hasMore := len(records) > pageSize
	if hasMore {
		records = records[:pageSize]
	}
	view := CommandSearchView{
		RequestID:   requestID(r),
		APIVersion:  currentCommandSearchVersion,
		Count:       len(records),
		PageSize:    pageSize,
		HasMore:     hasMore,
		GeneratedAt: time.Now().UTC(),
		Commands:    records,
	}
	if hasMore && len(records) > 0 {
		view.NextPageToken = encodeCommandPageToken(records[len(records)-1])
	}
	return view
}
