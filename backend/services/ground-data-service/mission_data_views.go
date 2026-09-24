package main

import (
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"
)

const currentMissionDataViewsVersion = "v0.9.93"

type MissionDataViewResponse struct {
	RequestID   string         `json:"request_id"`
	APIVersion  string         `json:"api_version"`
	MissionID   string         `json:"mission_id"`
	Kind        DataKind       `json:"kind"`
	Count       int            `json:"count"`
	Limit       int            `json:"limit"`
	GeneratedAt time.Time      `json:"generated_at"`
	Records     []GroundRecord `json:"records"`
}

func parseMissionDataViewLimit(r *http.Request) (int, error) {
	limit := 50
	if raw := strings.TrimSpace(r.URL.Query().Get("limit")); raw != "" {
		parsed, err := strconv.Atoi(raw)
		if err != nil || parsed < 1 || parsed > 500 {
			return 0, fmt.Errorf("limit must be between 1 and 500")
		}
		limit = parsed
	}
	return limit, nil
}

func buildMissionDataView(missionID string, kind DataKind, records []GroundRecord, limit int) MissionDataViewResponse {
	if limit > len(records) {
		limit = len(records)
	}
	if limit < 0 {
		limit = 0
	}
	return MissionDataViewResponse{
		APIVersion:  currentMissionDataViewsVersion,
		MissionID:   missionID,
		Kind:        kind,
		Count:       limit,
		Limit:       limit,
		GeneratedAt: time.Now().UTC(),
		Records:     records[:limit],
	}
}

func handleMissionDataView(w http.ResponseWriter, r *http.Request, store RecordStore, kind DataKind) {
	missionID := strings.TrimSpace(r.PathValue("missionID"))
	if missionID == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID is required"})
		return
	}
	repository, ok := store.(RecordRepository)
	if !ok {
		writeJSON(w, http.StatusNotImplemented, map[string]string{"error": "durable repository does not support mission data views"})
		return
	}
	limit, err := parseMissionDataViewLimit(r)
	if err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
		return
	}

	query := RecordQuery{MissionID: missionID, Kind: kind, Limit: limit}
	values := r.URL.Query()
	query.SourceNode = strings.TrimSpace(values.Get("source_node"))
	query.CorrelationID = strings.TrimSpace(values.Get("correlation_id"))
	records, err := repository.Query(r.Context(), query)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
		return
	}

	view := buildMissionDataView(missionID, kind, records, limit)
	view.RequestID = requestID(r)
	w.Header().Set("X-TRISHULA-API-VERSION", currentMissionDataViewsVersion)
	writeJSON(w, http.StatusOK, view)
}
