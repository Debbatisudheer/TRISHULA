package main

import (
	"net/http"
	"strings"
)

const currentGroundAPIContractVersion = "v0.9.105"

type GroundAPIContract struct {
	APIEnvelope
	Service         string                `json:"service"`
	Contract        string                `json:"contract"`
	ContractVersion string                `json:"contract_version"`
	ContentType     string                `json:"content_type"`
	RequestIDHeader string                `json:"request_id_header"`
	Resources       []APIResourceContract `json:"resources"`
	Error           APIErrorContract      `json:"error"`
}

type APIResourceContract struct {
	Method string `json:"method"`
	Path   string `json:"path"`
}

type APIErrorContract struct {
	Shape  string   `json:"shape"`
	Fields []string `json:"fields"`
}

func buildGroundAPIContract(requestIDValue string) GroundAPIContract {
	return GroundAPIContract{
		APIEnvelope:     APIEnvelope{RequestID: requestIDValue, Version: currentGroundAPIContractVersion},
		Service:         "trishula-ground-data-service",
		Contract:        "ground-api-v1",
		ContractVersion: currentGroundAPIContractVersion,
		ContentType:     "application/json",
		RequestIDHeader: "X-Request-ID",
		Resources: []APIResourceContract{
			{Method: "GET", Path: "/health"},
			{Method: "GET", Path: "/v1/api"},
			{Method: "GET", Path: "/v1/api/contract"},
			{Method: "GET", Path: "/v1/api/integration"},
			{Method: "GET", Path: "/v1/mission-control"},
			{Method: "GET", Path: "/v1/mission-control/state"},
			{Method: "GET", Path: "/v1/mission-control/dashboard"},
			{Method: "GET", Path: "/v1/mission-control/ws"},
			{Method: "GET", Path: "/v1/mission-control/alerts"},
			{Method: "GET", Path: "/v1/mission-control/alerts/{alertID}"},
			{Method: "GET", Path: "/v1/mission-control/alerts/{alertID}/detail"},
			{Method: "GET", Path: "/v1/mission-control/alerts/{alertID}/history"},
			{Method: "POST", Path: "/v1/mission-control/alerts/{alertID}/acknowledge"},
			{Method: "POST", Path: "/v1/mission-control/alerts/{alertID}/clear"},
			{Method: "GET", Path: "/v1/missions/{missionID}/runtime"},
			{Method: "POST", Path: "/v1/missions/{missionID}/runtime/{action}"},
			{Method: "GET", Path: "/v1/missions/{missionID}/commands"},
			{Method: "GET", Path: "/v1/missions/{missionID}/commands/{commandID}"},
			{Method: "GET", Path: "/v1/missions/{missionID}/commands/{commandID}/detail"},
			{Method: "GET", Path: "/v1/missions/{missionID}/telemetry"},
			{Method: "GET", Path: "/v1/missions/{missionID}/events"},
			{Method: "GET", Path: "/v1/missions/{missionID}/history"},
			{Method: "GET", Path: "/v1/missions/{missionID}/commands/{commandID}/history"},
			{Method: "GET", Path: "/v1/commands/search"},
			{Method: "GET", Path: "/v1/commands/execution/{commandID}"},
			{Method: "GET", Path: "/v1/commands/dlq"},
			{Method: "POST", Path: "/v1/missions/{missionID}/commands"},
			{Method: "POST", Path: "/v1/missions/{missionID}/commands/schedule"},
			{Method: "GET", Path: "/v1/commands/scheduled"},
			{Method: "GET", Path: "/v1/commands/scheduled/{scheduleID}"},
			{Method: "POST", Path: "/v1/commands/scheduled/{scheduleID}/cancel"},
			{Method: "GET", Path: "/v1/commands/dlq/{deadLetterID}"},
			{Method: "POST", Path: "/v1/commands/dlq/{deadLetterID}/replay"},
			{Method: "POST", Path: "/v1/commands/dlq/{deadLetterID}/resolve"},
		},
		Error: APIErrorContract{Shape: "{\"error\": \"message\"}", Fields: []string{"error"}},
	}
}

func handleGroundAPIContract(w http.ResponseWriter, r *http.Request) {
	if method := strings.ToUpper(r.Method); method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		return
	}
	w.Header().Set("X-TRISHULA-API-VERSION", currentGroundAPIContractVersion)
	writeJSON(w, http.StatusOK, buildGroundAPIContract(requestID(r)))
}
