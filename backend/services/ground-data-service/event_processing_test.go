package main

import "testing"

func TestRecordRouterEventLifecycle(t *testing.T) {
	router := NewRecordRouter()
	fault := eventRecord("ROUTER-E-001", "MOTOR_FAULT", "CRITICAL")
	if err := router.Route(fault); err != nil {
		t.Fatalf("route fault: %v", err)
	}
	results := router.Results()
	result, ok := results[KindEvent]
	if !ok {
		t.Fatal("missing event processing result")
	}
	if result.Attributes["severity"] != "CRITICAL" || result.Attributes["lifecycle"] != "ACTIVE" {
		t.Fatalf("unexpected result: %+v", result)
	}

	recovery := eventRecord("ROUTER-E-002", "RECOVERY", "INFO")
	recovery.Fields["event_class"] = "RECOVERY"
	recovery.Fields["recovery_of"] = "MOTOR_FAULT"
	recovery.Fields["fault_code"] = ""
	recovery.Fields["lifecycle"] = "CLEARED"
	if err := router.Route(recovery); err != nil {
		t.Fatalf("route recovery: %v", err)
	}
	result, ok = router.Results()[KindEvent]
	if !ok || result.Attributes["lifecycle"] != "CLEARED" {
		t.Fatalf("unexpected recovery result: %+v", result)
	}
}
