package main

import (
	"net/http/httptest"
	"testing"
)

func TestParseOperationalHistoryQueryDefaults(t *testing.T) {
	req := httptest.NewRequest("GET", "/v1/missions/TRISHULA/history", nil)
	query, err := parseOperationalHistoryQuery(req, "TRISHULA")
	if err != nil {
		t.Fatal(err)
	}
	if query.MissionID != "TRISHULA" {
		t.Fatalf("mission = %q", query.MissionID)
	}
	if query.Limit != 100 {
		t.Fatalf("limit = %d, want 100", query.Limit)
	}
}

func TestParseHistoryLimit(t *testing.T) {
	if got, err := parseHistoryLimit(""); err != nil || got != 100 {
		t.Fatalf("default = %d, %v", got, err)
	}
	if got, err := parseHistoryLimit("500"); err != nil || got != 500 {
		t.Fatalf("500 = %d, %v", got, err)
	}
	if _, err := parseHistoryLimit("501"); err == nil {
		t.Fatal("expected upper-bound error")
	}
}

func TestBuildOperationalHistoryView(t *testing.T) {
	view := buildOperationalHistoryView("TRISHULA", nil, 100)
	if view.APIVersion != currentOperationalHistoryVersion {
		t.Fatalf("version = %q", view.APIVersion)
	}
	if view.MissionID != "TRISHULA" {
		t.Fatalf("mission = %q", view.MissionID)
	}
	if view.GeneratedAt.IsZero() {
		t.Fatal("generated_at should be set")
	}
}
