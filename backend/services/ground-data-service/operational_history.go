package main

import (
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"
)

const currentOperationalHistoryVersion = "v0.9.89"

type OperationalHistoryView struct {
	RequestID   string         `json:"request_id"`
	APIVersion  string         `json:"api_version"`
	MissionID   string         `json:"mission_id"`
	CommandID   string         `json:"command_id,omitempty"`
	Count       int            `json:"count"`
	HasMore     bool           `json:"has_more"`
	GeneratedAt time.Time      `json:"generated_at"`
	Records     []GroundRecord `json:"records"`
}

func parseOperationalHistoryQuery(r *http.Request, missionID string) (RecordQuery, error) {
	if r == nil {
		return RecordQuery{}, fmt.Errorf("request is required")
	}
	query, err := parseRecordQuery(r)
	if err != nil {
		return RecordQuery{}, err
	}
	query.MissionID = missionID
	if query.Limit == 0 {
		query.Limit = 100
	}
	if query.Limit > 500 {
		query.Limit = 500
	}
	return query, nil
}

func buildOperationalHistoryView(missionID string, records []GroundRecord, limit int) OperationalHistoryView {
	hasMore := limit > 0 && len(records) >= limit
	return OperationalHistoryView{
		APIVersion:  currentOperationalHistoryVersion,
		MissionID:   missionID,
		Count:       len(records),
		HasMore:     hasMore,
		GeneratedAt: time.Now().UTC(),
		Records:     records,
	}
}

func parseHistoryLimit(raw string) (int, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return 100, nil
	}
	value, err := strconv.Atoi(raw)
	if err != nil || value < 1 || value > 500 {
		return 0, fmt.Errorf("limit must be between 1 and 500")
	}
	return value, nil
}
