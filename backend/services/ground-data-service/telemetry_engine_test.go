package main

import "testing"

func TestTelemetryEngineBatteryNominal(t *testing.T) {
	record := sampleRecord("TEL-ENG-001", 1, KindTelemetry)
	record.Envelope.Quality = 0.99
	record.Fields = map[string]string{
		"metric": "battery",
		"value":  "85.4",
		"unit":   "%",
	}

	result, err := processTelemetryOutput(record)
	if err != nil {
		t.Fatal(err)
	}
	if result.Operation != "telemetry-evaluate:battery" {
		t.Fatalf("unexpected operation: %s", result.Operation)
	}
	if result.Attributes["unit"] != "%" || result.Attributes["quality_class"] != "GOOD" {
		t.Fatalf("unexpected normalization/quality: %+v", result.Attributes)
	}
	if result.Attributes["limit_status"] != "NORMAL" || result.Attributes["operational_state"] != "NOMINAL" {
		t.Fatalf("unexpected operational evaluation: %+v", result.Attributes)
	}
}

func TestTelemetryEngineTemperatureConvertsCelsiusToKelvin(t *testing.T) {
	record := sampleRecord("TEL-ENG-002", 2, KindTelemetry)
	record.Fields = map[string]string{
		"metric": "temperature",
		"value":  "25",
		"unit":   "C",
	}

	result, err := processTelemetryOutput(record)
	if err != nil {
		t.Fatal(err)
	}
	if result.Attributes["unit"] != "K" {
		t.Fatalf("expected K, got %+v", result.Attributes)
	}
	if result.Attributes["value"] != "298.15" {
		t.Fatalf("expected 298.15 K, got %+v", result.Attributes)
	}
	if result.Attributes["input_unit"] != "C" {
		t.Fatalf("expected input unit C, got %+v", result.Attributes)
	}
}

func TestTelemetryEngineQualityAndBatteryLowState(t *testing.T) {
	record := sampleRecord("TEL-ENG-003", 3, KindTelemetry)
	record.Envelope.Quality = 0.84
	record.Fields = map[string]string{
		"metric": "battery",
		"value":  "25",
	}

	result, err := processTelemetryOutput(record)
	if err != nil {
		t.Fatal(err)
	}
	if result.Attributes["quality_class"] != "DEGRADED" {
		t.Fatalf("expected DEGRADED quality, got %+v", result.Attributes)
	}
	if result.Attributes["limit_status"] != "LOW" {
		t.Fatalf("expected LOW limit status, got %+v", result.Attributes)
	}
	if result.Attributes["operational_state"] != "LOW_POWER" {
		t.Fatalf("expected LOW_POWER state, got %+v", result.Attributes)
	}
}

func TestTelemetryEngineRejectsInvalidNumericValue(t *testing.T) {
	record := sampleRecord("TEL-ENG-004", 4, KindTelemetry)
	record.Fields = map[string]string{
		"metric": "battery",
		"value":  "not-a-number",
	}

	if _, err := processTelemetryOutput(record); err == nil {
		t.Fatal("expected invalid numeric telemetry to be rejected")
	}
}

func TestTelemetryEngineRejectsOutOfRangeBattery(t *testing.T) {
	record := sampleRecord("TEL-ENG-005", 5, KindTelemetry)
	record.Fields = map[string]string{
		"metric": "battery",
		"value":  "120",
	}

	if _, err := processTelemetryOutput(record); err == nil {
		t.Fatal("expected out-of-range battery telemetry to be rejected")
	}
}

func TestTelemetryEngineHotTemperatureState(t *testing.T) {
	record := sampleRecord("TEL-ENG-006", 6, KindTelemetry)
	record.Fields = map[string]string{
		"metric": "temperature",
		"value":  "350",
		"unit":   "K",
	}

	result, err := processTelemetryOutput(record)
	if err != nil {
		t.Fatal(err)
	}
	if result.Attributes["limit_status"] != "HIGH" || result.Attributes["operational_state"] != "HOT" {
		t.Fatalf("expected HOT/HIGH state, got %+v", result.Attributes)
	}
}
