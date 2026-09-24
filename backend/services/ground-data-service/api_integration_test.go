package main

import (
	"net/http/httptest"
	"testing"
)

func TestRequestIDPreservesValidHeader(t *testing.T) {
	r := httptest.NewRequest("GET", "/v1/api", nil)
	r.Header.Set("X-Request-ID", "integration-test-001")
	if got := requestID(r); got != "integration-test-001" {
		t.Fatalf("requestID = %q", got)
	}
}

func TestRequestIDGeneratesWhenMissing(t *testing.T) {
	r := httptest.NewRequest("GET", "/v1/api", nil)
	if got := requestID(r); len(got) != 32 {
		t.Fatalf("generated request ID length = %d, want 32", len(got))
	}
}
