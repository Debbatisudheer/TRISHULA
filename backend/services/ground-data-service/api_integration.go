package main

import (
	"crypto/rand"
	"encoding/hex"
	"net/http"
	"strings"
)

const currentGroundAPIIntegrationVersion = "v0.9.105"

// APIEnvelope is the stable transport envelope for API metadata. Existing
// resource endpoints remain backward-compatible; this envelope is used by
// integration/contract endpoints introduced in v0.9.89.
type APIEnvelope struct {
	RequestID string `json:"request_id"`
	Version   string `json:"api_version"`
}

type GroundAPIInfo struct {
	APIEnvelope
	Service   string   `json:"service"`
	Contract  string   `json:"contract"`
	Resources []string `json:"resources"`
}

type CommandIntegrationStatus struct {
	APIEnvelope
	MissionID   string                          `json:"mission_id"`
	CommandID   string                          `json:"command_id"`
	Lifecycle   *CommandLifecycleEvaluation     `json:"lifecycle,omitempty"`
	Execution   *CommandExecutionReconciliation `json:"execution,omitempty"`
	DeadLetter  *CommandDeadLetterEntry         `json:"dead_letter,omitempty"`
	RecordCount int                             `json:"record_count"`
}

func requestID(r *http.Request) string {
	if r != nil {
		if id := strings.TrimSpace(r.Header.Get("X-Request-ID")); id != "" && len(id) <= 128 {
			return id
		}
	}
	var raw [16]byte
	if _, err := rand.Read(raw[:]); err == nil {
		return hex.EncodeToString(raw[:])
	}
	return "trishula-request"
}

func withRequestID(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		id := requestID(r)
		w.Header().Set("X-Request-ID", id)
		next.ServeHTTP(w, r)
	})
}
