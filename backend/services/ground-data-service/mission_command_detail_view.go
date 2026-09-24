package main

import (
	"net/http"
	"strings"
	"time"
)

const currentMissionCommandDetailViewVersion = "v0.9.92"

type MissionCommandDetailView struct {
	RequestID   string                          `json:"request_id"`
	APIVersion  string                          `json:"api_version"`
	MissionID   string                          `json:"mission_id"`
	CommandID   string                          `json:"command_id"`
	Command     string                          `json:"command"`
	Target      string                          `json:"target"`
	Lifecycle   *CommandLifecycleEvaluation     `json:"lifecycle,omitempty"`
	Execution   *CommandExecutionReconciliation `json:"execution,omitempty"`
	DeadLetter  *CommandDeadLetterEntry         `json:"dead_letter,omitempty"`
	RecordCount int                             `json:"record_count"`
	Records     []GroundRecord                  `json:"records"`
	Terminal    bool                            `json:"terminal"`
	GeneratedAt time.Time                       `json:"generated_at"`
}

func buildMissionCommandDetailView(missionID, commandID string, records []GroundRecord, lifecycle *CommandLifecycleEvaluation, execution *CommandExecutionReconciliation, deadLetter *CommandDeadLetterEntry) MissionCommandDetailView {
	view := MissionCommandDetailView{
		APIVersion:  currentMissionCommandDetailViewVersion,
		MissionID:   missionID,
		CommandID:   commandID,
		Lifecycle:   lifecycle,
		Execution:   execution,
		DeadLetter:  deadLetter,
		RecordCount: len(records),
		Records:     records,
		GeneratedAt: time.Now().UTC(),
	}
	if lifecycle != nil {
		view.Command = strings.TrimSpace(lifecycle.Command)
		view.Target = strings.TrimSpace(lifecycle.Target)
		view.Terminal = lifecycle.Terminal
	}
	if execution != nil {
		if view.Command == "" {
			view.Command = strings.TrimSpace(execution.Command)
		}
		if view.Target == "" {
			view.Target = strings.TrimSpace(execution.Target)
		}
		view.Terminal = execution.Terminal
	}
	if view.Command == "" || view.Target == "" {
		for _, record := range records {
			if view.Command == "" {
				view.Command = strings.TrimSpace(record.Fields["command"])
			}
			if view.Target == "" {
				view.Target = strings.TrimSpace(record.Fields["target"])
			}
			if view.Command != "" && view.Target != "" {
				break
			}
		}
	}
	if !view.Terminal {
		lifecycleValue := ""
		if lifecycle != nil {
			lifecycleValue = string(lifecycle.Lifecycle)
		}
		if lifecycleValue == "" && execution != nil {
			lifecycleValue = string(execution.State)
		}
		if isMissionCommandTerminal(lifecycleValue) {
			view.Terminal = true
		}
	}
	if !view.Terminal {
		// The service may restart without an in-memory lifecycle/reconciliation
		// projection. Durable command/event records are still authoritative for
		// determining whether this command reached a terminal execution state.
		for _, record := range records {
			state := strings.TrimSpace(record.Fields["execution_state"])
			if state == "" {
				state = strings.TrimSpace(record.Fields["lifecycle"])
			}
			if isMissionCommandTerminal(state) {
				view.Terminal = true
				break
			}
		}
	}
	return view
}

func handleMissionCommandDetailView(w http.ResponseWriter, r *http.Request, store RecordStore, consumer *KafkaConsumer, deadLetterQueue *CommandDeadLetterQueue) {
	missionID := strings.TrimSpace(r.PathValue("missionID"))
	commandID := strings.TrimSpace(r.PathValue("commandID"))
	if missionID == "" || commandID == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "missionID and commandID are required"})
		return
	}
	repository, ok := store.(RecordRepository)
	if !ok {
		writeJSON(w, http.StatusNotImplemented, map[string]string{"error": "durable repository does not support command detail queries"})
		return
	}
	records, err := repository.Query(r.Context(), RecordQuery{MissionID: missionID, CorrelationID: commandID, Limit: 200})
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": err.Error()})
		return
	}

	var lifecycle *CommandLifecycleEvaluation
	if consumer != nil {
		for _, item := range consumer.router.CommandLifecycle() {
			if item.CommandID == commandID {
				copy := item
				lifecycle = &copy
				break
			}
		}
	}
	var execution *CommandExecutionReconciliation
	if consumer != nil {
		if item, ok := consumer.router.CommandExecutionByID(commandID); ok {
			copy := item
			execution = &copy
		}
	}
	var deadLetter *CommandDeadLetterEntry
	if deadLetterQueue != nil {
		for _, item := range deadLetterQueue.List(missionID) {
			if item.CommandID == commandID {
				copy := item
				deadLetter = &copy
				break
			}
		}
	}
	if len(records) == 0 && lifecycle == nil && execution == nil && deadLetter == nil {
		writeJSON(w, http.StatusNotFound, map[string]string{"error": "command not found"})
		return
	}

	view := buildMissionCommandDetailView(missionID, commandID, records, lifecycle, execution, deadLetter)
	view.RequestID = requestID(r)
	w.Header().Set("X-TRISHULA-API-VERSION", currentMissionCommandDetailViewVersion)
	writeJSON(w, http.StatusOK, view)
}
