package main

import (
	"net/http"
	"strings"
)

const currentGroundAPIIntegrationStatusVersion = "v0.9.105"

type GroundAPIIntegrationStatus struct {
	APIEnvelope
	Service         string                         `json:"service"`
	Contract        string                         `json:"contract"`
	ContractVersion string                         `json:"contract_version"`
	Status          string                         `json:"status"`
	Components      GroundAPIIntegrationComponents `json:"components"`
}

type GroundAPIIntegrationComponents struct {
	Storage        IntegrationComponent `json:"storage"`
	RedisCache     IntegrationComponent `json:"redis_cache"`
	KafkaPublisher IntegrationComponent `json:"kafka_publisher"`
	KafkaConsumer  IntegrationComponent `json:"kafka_consumer"`
	Orchestrator   IntegrationComponent `json:"command_orchestrator"`
	Scheduler      IntegrationComponent `json:"command_scheduler"`
	DeadLetter     IntegrationComponent `json:"dead_letter_queue"`
}

type IntegrationComponent struct {
	Status string `json:"status"`
}

func buildGroundAPIIntegrationStatus(requestIDValue string, storage, cache, publisher, consumer bool, orchestrator, scheduler, deadLetter bool) GroundAPIIntegrationStatus {
	component := func(enabled bool) IntegrationComponent {
		if enabled {
			return IntegrationComponent{Status: "configured"}
		}
		return IntegrationComponent{Status: "disabled"}
	}
	statuses := []string{
		component(storage).Status,
		component(cache).Status,
		component(publisher).Status,
		component(consumer).Status,
		component(orchestrator).Status,
		component(scheduler).Status,
		component(deadLetter).Status,
	}
	status := "ready"
	for _, value := range statuses {
		if value == "disabled" {
			status = "degraded"
			break
		}
	}
	return GroundAPIIntegrationStatus{
		APIEnvelope:     APIEnvelope{RequestID: requestIDValue, Version: currentGroundAPIIntegrationStatusVersion},
		Service:         "trishula-ground-data-service",
		Contract:        "ground-api-v1",
		ContractVersion: currentGroundAPIIntegrationStatusVersion,
		Status:          status,
		Components: GroundAPIIntegrationComponents{
			Storage:        component(storage),
			RedisCache:     component(cache),
			KafkaPublisher: component(publisher),
			KafkaConsumer:  component(consumer),
			Orchestrator:   component(orchestrator),
			Scheduler:      component(scheduler),
			DeadLetter:     component(deadLetter),
		},
	}
}

func handleGroundAPIIntegrationStatus(w http.ResponseWriter, r *http.Request, storage, cache, publisher, consumer bool, orchestrator, scheduler, deadLetter bool) {
	if method := strings.ToUpper(r.Method); method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		return
	}
	w.Header().Set("X-TRISHULA-API-VERSION", currentGroundAPIIntegrationStatusVersion)
	writeJSON(w, http.StatusOK, buildGroundAPIIntegrationStatus(requestID(r), storage, cache, publisher, consumer, orchestrator, scheduler, deadLetter))
}
