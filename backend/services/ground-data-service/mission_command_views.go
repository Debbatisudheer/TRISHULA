package main

import (
	"fmt"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"time"
)

const currentMissionCommandViewsVersion = "v0.9.91"

type MissionCommandView struct {
	CommandID    string                          `json:"command_id"`
	Command      string                          `json:"command"`
	Target       string                          `json:"target"`
	Lifecycle    string                          `json:"lifecycle"`
	Terminal     bool                            `json:"terminal"`
	LatestRecord GroundRecord                    `json:"latest_record"`
	Execution    *CommandExecutionReconciliation `json:"execution,omitempty"`
	DeadLetter   *CommandDeadLetterEntry         `json:"dead_letter,omitempty"`
}

type MissionCommandViewsResponse struct {
	RequestID   string               `json:"request_id"`
	APIVersion  string               `json:"api_version"`
	MissionID   string               `json:"mission_id"`
	Count       int                  `json:"count"`
	Limit       int                  `json:"limit"`
	GeneratedAt time.Time            `json:"generated_at"`
	Commands    []MissionCommandView `json:"commands"`
}

func parseMissionCommandViewLimit(r *http.Request) (int, error) {
	limit := 50
	if raw := strings.TrimSpace(r.URL.Query().Get("limit")); raw != "" {
		parsed, err := strconv.Atoi(raw)
		if err != nil || parsed < 1 || parsed > 200 {
			return 0, fmt.Errorf("limit must be between 1 and 200")
		}
		limit = parsed
	}
	return limit, nil
}

func buildMissionCommandViews(missionID string, records []GroundRecord, executions []CommandExecutionReconciliation, dlq []CommandDeadLetterEntry, limit int) MissionCommandViewsResponse {
	type aggregate struct {
		latest GroundRecord
		set    bool
	}
	grouped := make(map[string]aggregate)
	for _, record := range records {
		id := strings.TrimSpace(record.Envelope.CorrelationID)
		if id == "" {
			if value := strings.TrimSpace(record.Fields["command_id"]); value != "" {
				id = value
			} else {
				id = strings.TrimSpace(record.Envelope.RecordID)
			}
		}
		if id == "" {
			continue
		}
		current := grouped[id]
		if !current.set || commandViewRecordNewer(record, current.latest) {
			current.latest = record
			current.set = true
		}
		grouped[id] = current
	}

	executionByID := make(map[string]CommandExecutionReconciliation, len(executions))
	for _, execution := range executions {
		executionByID[execution.CommandID] = execution
	}
	dlqByID := make(map[string]CommandDeadLetterEntry, len(dlq))
	for _, entry := range dlq {
		dlqByID[entry.CommandID] = entry
	}

	views := make([]MissionCommandView, 0, len(grouped))
	for commandID, aggregate := range grouped {
		record := aggregate.latest
		lifecycle := strings.TrimSpace(record.Fields["lifecycle"])
		command := strings.TrimSpace(record.Fields["command"])
		target := strings.TrimSpace(record.Fields["target"])
		if execution, ok := executionByID[commandID]; ok {
			copy := execution
			if command == "" {
				command = execution.Command
			}
			if target == "" {
				target = execution.Target
			}
			lifecycle = string(execution.State)
			views = append(views, MissionCommandView{CommandID: commandID, Command: command, Target: target, Lifecycle: lifecycle, Terminal: execution.Terminal, LatestRecord: record, Execution: &copy})
		} else {
			terminal := isMissionCommandTerminal(lifecycle)
			view := MissionCommandView{CommandID: commandID, Command: command, Target: target, Lifecycle: lifecycle, Terminal: terminal, LatestRecord: record}
			if entry, ok := dlqByID[commandID]; ok {
				copy := entry
				view.DeadLetter = &copy
			}
			views = append(views, view)
		}
	}
	sort.Slice(views, func(i, j int) bool {
		a, b := views[i].LatestRecord.Envelope, views[j].LatestRecord.Envelope
		if a.MissionTimestamp != b.MissionTimestamp {
			return a.MissionTimestamp > b.MissionTimestamp
		}
		if a.SequenceNumber != b.SequenceNumber {
			return a.SequenceNumber > b.SequenceNumber
		}
		return views[i].CommandID < views[j].CommandID
	})
	if limit > len(views) {
		limit = len(views)
	}
	if limit < 0 {
		limit = 0
	}
	return MissionCommandViewsResponse{
		RequestID:   "",
		APIVersion:  currentMissionCommandViewsVersion,
		MissionID:   missionID,
		Count:       limit,
		Limit:       limit,
		GeneratedAt: time.Now().UTC(),
		Commands:    views[:limit],
	}
}

func commandViewRecordNewer(a, b GroundRecord) bool {
	if a.Envelope.MissionTimestamp != b.Envelope.MissionTimestamp {
		return a.Envelope.MissionTimestamp > b.Envelope.MissionTimestamp
	}
	if a.Envelope.SequenceNumber != b.Envelope.SequenceNumber {
		return a.Envelope.SequenceNumber > b.Envelope.SequenceNumber
	}
	return a.Envelope.RecordID > b.Envelope.RecordID
}

func isMissionCommandTerminal(lifecycle string) bool {
	switch strings.ToUpper(strings.TrimSpace(lifecycle)) {
	case "COMPLETED", "FAILED", "REJECTED", "TIMEOUT", "CANCELLED":
		return true
	default:
		return false
	}
}
