package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestBuildGroundAPIIntegrationStatus(t *testing.T) {
	status := buildGroundAPIIntegrationStatus("integration-test-001", true, true, true, true, true, true, true)
	if status.Version != "v0.9.105" || status.ContractVersion != "v0.9.105" {
		t.Fatalf("versions = %q/%q", status.Version, status.ContractVersion)
	}
	if status.Status != "ready" {
		t.Fatalf("status = %q", status.Status)
	}
	if status.Components.Orchestrator.Status != "configured" {
		t.Fatalf("orchestrator = %q", status.Components.Orchestrator.Status)
	}
}

func TestBuildGroundAPIIntegrationStatusDegraded(t *testing.T) {
	status := buildGroundAPIIntegrationStatus("integration-test-002", true, false, true, true, true, true, true)
	if status.Status != "degraded" {
		t.Fatalf("status = %q", status.Status)
	}
	if status.Components.RedisCache.Status != "disabled" {
		t.Fatalf("redis cache = %q", status.Components.RedisCache.Status)
	}
}

func TestHandleGroundAPIIntegrationStatusRejectsNonGet(t *testing.T) {
	r := httptest.NewRequest(http.MethodPost, "/v1/api/integration", nil)
	w := httptest.NewRecorder()
	handleGroundAPIIntegrationStatus(w, r, true, true, true, true, true, true, true)
	if w.Code != http.StatusMethodNotAllowed {
		t.Fatalf("status = %d", w.Code)
	}
	if !strings.Contains(w.Body.String(), "method not allowed") {
		t.Fatalf("body = %s", w.Body.String())
	}
}
