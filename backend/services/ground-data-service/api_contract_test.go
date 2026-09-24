package main

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestBuildGroundAPIContract(t *testing.T) {
	contract := buildGroundAPIContract("contract-test-001")
	if contract.Version != "v0.9.105" || contract.ContractVersion != "v0.9.105" {
		t.Fatalf("contract version = %q/%q", contract.Version, contract.ContractVersion)
	}
	if contract.Contract != "ground-api-v1" {
		t.Fatalf("contract = %q", contract.Contract)
	}
	if len(contract.Resources) == 0 {
		t.Fatal("contract resources are empty")
	}
}

func TestHandleGroundAPIContract(t *testing.T) {
	r := httptest.NewRequest(http.MethodGet, "/v1/api/contract", nil)
	r.Header.Set("X-Request-ID", "contract-test-002")
	w := httptest.NewRecorder()
	handleGroundAPIContract(w, r)
	if w.Code != http.StatusOK {
		t.Fatalf("status = %d", w.Code)
	}
	if got := w.Header().Get("X-TRISHULA-API-VERSION"); got != "v0.9.105" {
		t.Fatalf("api version header = %q", got)
	}
	if !strings.Contains(w.Body.String(), `"request_id":"contract-test-002"`) {
		t.Fatalf("response did not preserve request id: %s", w.Body.String())
	}
}

func TestHandleGroundAPIContractRejectsNonGet(t *testing.T) {
	r := httptest.NewRequest(http.MethodPost, "/v1/api/contract", nil)
	w := httptest.NewRecorder()
	handleGroundAPIContract(w, r)
	if w.Code != http.StatusMethodNotAllowed {
		t.Fatalf("status = %d", w.Code)
	}
}

func TestGroundAPIContractMissionControlResourceCompleteness(t *testing.T) {
	contract := buildGroundAPIContract("contract-test-004")
	expected := []APIResourceContract{
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
	}
	present := make(map[APIResourceContract]bool, len(contract.Resources))
	for _, resource := range contract.Resources {
		present[resource] = true
	}
	for _, resource := range expected {
		if !present[resource] {
			t.Fatalf("missing Mission Control resource: %+v", resource)
		}
	}
}

func TestGroundAPIContractResourceInventoryIsDeterministic(t *testing.T) {
	first := buildGroundAPIContract("contract-test-005")
	second := buildGroundAPIContract("contract-test-006")
	if len(first.Resources) != len(second.Resources) {
		t.Fatalf("resource count changed: %d vs %d", len(first.Resources), len(second.Resources))
	}
	for i := range first.Resources {
		if first.Resources[i] != second.Resources[i] {
			t.Fatalf("resource %d changed: %+v vs %+v", i, first.Resources[i], second.Resources[i])
		}
	}
}
