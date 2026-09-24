package main

import "testing"

func eventRecord(id, eventType, severity string) GroundRecord {
	r := sampleRecord(id, 1, KindEvent)
	r.Fields = map[string]string{
		"event_type":  eventType,
		"severity":    severity,
		"subsystem":   "thermal",
		"event_class": "FAULT",
		"fault_code":  "THERMAL_OVER_TEMP",
	}
	return r
}

func TestEventEngineNormalizesSeverityAndLifecycle(t *testing.T) {
	engine := NewEventEngine()
	evaluation, err := engine.Evaluate(eventRecord("E-1", "THERMAL_WARNING", "warning"))
	if err != nil {
		t.Fatalf("evaluate: %v", err)
	}
	if evaluation.Severity != EventWarning || evaluation.SeverityRank != 2 {
		t.Fatalf("unexpected severity: %+v", evaluation)
	}
	if evaluation.Lifecycle != EventActive || !evaluation.IsFault {
		t.Fatalf("unexpected lifecycle/fault: %+v", evaluation)
	}
	if len(engine.ActiveFaults()) != 1 {
		t.Fatalf("expected one active fault, got %d", len(engine.ActiveFaults()))
	}
}

func TestEventEngineCriticalAndRecovery(t *testing.T) {
	engine := NewEventEngine()
	fault := eventRecord("E-2", "THERMAL_CRITICAL", "critical")
	if _, err := engine.Evaluate(fault); err != nil {
		t.Fatalf("fault evaluate: %v", err)
	}
	recovery := eventRecord("E-3", "RECOVERY", "info")
	recovery.Fields["event_class"] = "RECOVERY"
	recovery.Fields["recovery_of"] = "THERMAL_OVER_TEMP"
	recovery.Fields["fault_code"] = ""
	recovery.Fields["lifecycle"] = "CLEARED"
	if _, err := engine.Evaluate(recovery); err != nil {
		t.Fatalf("recovery evaluate: %v", err)
	}
	if len(engine.ActiveFaults()) != 0 {
		t.Fatalf("expected no active faults after recovery, got %d", len(engine.ActiveFaults()))
	}
}

func TestEventEngineRejectsInvalidSeverity(t *testing.T) {
	engine := NewEventEngine()
	r := eventRecord("E-4", "THERMAL_WARNING", "SEVERE")
	if _, err := engine.Evaluate(r); err == nil {
		t.Fatal("expected invalid severity error")
	}
}

func TestEventProcessingOutput(t *testing.T) {
	engine := NewEventEngine()
	result, err := processEventEngineOutput(eventRecord("E-5", "BATTERY_LOW", "ERROR"), engine)
	if err != nil {
		t.Fatalf("process output: %v", err)
	}
	if result.Status != "accepted" || result.ProcessorVersion != currentProcessorVersion {
		t.Fatalf("unexpected result: %+v", result)
	}
	if result.Attributes["severity"] != "ERROR" || result.Attributes["lifecycle"] != "ACTIVE" {
		t.Fatalf("unexpected attributes: %+v", result.Attributes)
	}
	if result.Attributes["active_fault_count"] != "1" {
		t.Fatalf("expected active fault count 1, got %q", result.Attributes["active_fault_count"])
	}
}
