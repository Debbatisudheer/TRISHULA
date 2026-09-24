package main

import (
	"net/http/httptest"
	"testing"
)

func TestBuildMissionDataView(t *testing.T) {
	records := []GroundRecord{
		testGroundRecord("TEL-2", KindTelemetry, 2),
		testGroundRecord("TEL-1", KindTelemetry, 1),
	}
	view := buildMissionDataView("TRISHULA", KindTelemetry, records, 1)
	if view.APIVersion != "v0.9.93" {
		t.Fatalf("api version = %q", view.APIVersion)
	}
	if view.Count != 1 || len(view.Records) != 1 {
		t.Fatalf("count=%d records=%d", view.Count, len(view.Records))
	}
	if view.Records[0].Envelope.RecordID != "TEL-2" {
		t.Fatalf("unexpected record %q", view.Records[0].Envelope.RecordID)
	}
}

func TestParseMissionDataViewLimit(t *testing.T) {
	req := httptest.NewRequest("GET", "/?limit=200", nil)
	limit, err := parseMissionDataViewLimit(req)
	if err != nil || limit != 200 {
		t.Fatalf("limit=%d err=%v", limit, err)
	}
	req = httptest.NewRequest("GET", "/?limit=501", nil)
	if _, err := parseMissionDataViewLimit(req); err == nil {
		t.Fatal("expected limit validation error")
	}
}
