package main

import (
	"testing"
	"time"
)

func missionTelemetryRecord(id string, seq uint64, metric, value, unit, opState, qualityClass, limitStatus string) GroundRecord {
	r := sampleRecord(id, seq, KindTelemetry)
	r.Fields = map[string]string{
		"metric":            metric,
		"value":             value,
		"unit":              unit,
		"quality_class":     qualityClass,
		"limit_status":      limitStatus,
		"operational_state": opState,
		"quality":           "0.990000",
	}
	return r
}

func TestMissionStateEngineAggregatesTelemetryAndHealth(t *testing.T) {
	engine := NewMissionStateEngine()
	result := buildProcessingResult(missionTelemetryRecord("M-1", 1, "battery", "87.5", "%", "NOMINAL", "GOOD", "NORMAL"), "telemetry-evaluate:battery", map[string]string{
		"metric": "battery", "value": "87.5", "unit": "%", "quality": "0.990000", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	if err := engine.Apply(result); err != nil {
		t.Fatal(err)
	}
	mission, ok := engine.Get("TRISHULA")
	if !ok {
		t.Fatal("mission state not created")
	}
	rover, ok := mission.Vehicles["ROVER-01"]
	if !ok {
		t.Fatal("rover state not created")
	}
	if rover.LastSequence != 1 || rover.HealthStatus != "NOMINAL" {
		t.Fatalf("unexpected rover state: %+v", rover)
	}
	if rover.Metrics["battery"].Value != "87.5" {
		t.Fatalf("missing battery metric: %+v", rover.Metrics)
	}
}

func TestMissionStateDoesNotRegressMetricOnOlderSequence(t *testing.T) {
	engine := NewMissionStateEngine()
	newer := buildProcessingResult(missionTelemetryRecord("NEW", 10, "battery", "80", "%", "NOMINAL", "GOOD", "NORMAL"), "telemetry-evaluate:battery", map[string]string{
		"metric": "battery", "value": "80", "unit": "%", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	older := buildProcessingResult(missionTelemetryRecord("OLD", 9, "battery", "60", "%", "LOW_POWER", "DEGRADED", "LOW"), "telemetry-evaluate:battery", map[string]string{
		"metric": "battery", "value": "60", "unit": "%", "quality_class": "DEGRADED", "limit_status": "LOW", "operational_state": "LOW_POWER",
	})
	if err := engine.Apply(newer); err != nil {
		t.Fatal(err)
	}
	if err := engine.Apply(older); err != nil {
		t.Fatal(err)
	}
	mission, _ := engine.Get("TRISHULA")
	rover := mission.Vehicles["ROVER-01"]
	if rover.LastSequence != 10 {
		t.Fatalf("state sequence regressed: %+v", rover)
	}
	if rover.Metrics["battery"].Value != "80" {
		t.Fatalf("metric regressed: %+v", rover.Metrics["battery"])
	}
}

func TestMissionStateAggregatesScienceEventCommandAndFile(t *testing.T) {
	router := NewRecordRouter()
	records := []GroundRecord{
		sampleRecord("MS-T", 1, KindTelemetry),
		sampleRecord("MS-S", 2, KindScience),
		sampleRecord("MS-E", 3, KindEvent),
		sampleRecord("MS-C", 4, KindCommand),
		sampleRecord("MS-F", 5, KindFile),
	}
	records[0].Fields = map[string]string{"metric": "temperature", "value": "298.15", "unit": "K", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL", "quality": "0.99"}
	records[1].Fields = map[string]string{"instrument": "LIBS", "target": "TARGET-7", "measurement": "elemental_abundance", "quality_class": "GOOD", "scientific_usability": "USABLE"}
	records[2].Fields = map[string]string{"event_type": "THERMAL_WARNING", "severity": "WARNING", "event_class": "FAULT", "fault_code": "T-HIGH", "subsystem": "thermal"}
	records[3].Fields = map[string]string{"command_id": "CMD-1", "command": "STOP_ROVER", "target": "ROVER-01", "lifecycle": "VALIDATED"}
	records[4].Fields = map[string]string{"filename": "image.dat", "media_type": "science-data", "size_bytes": "4096"}
	for _, r := range records {
		if err := router.Route(r); err != nil {
			t.Fatalf("route %s: %v", r.Envelope.RecordID, err)
		}
	}
	mission, ok := router.Mission("TRISHULA")
	if !ok {
		t.Fatal("mission not found")
	}
	rover := mission.Vehicles["ROVER-01"]
	if rover.LastSequence != 5 {
		t.Fatalf("unexpected last sequence: %d", rover.LastSequence)
	}
	if rover.LastScience == nil || rover.LastScience.Instrument != "LIBS" {
		t.Fatalf("science state missing: %+v", rover.LastScience)
	}
	if rover.LastEvent == nil || rover.LastEvent.Severity != "WARNING" {
		t.Fatalf("event state missing: %+v", rover.LastEvent)
	}
	if rover.LastCommand == nil || rover.LastCommand.Lifecycle != string(CommandValidated) {
		t.Fatalf("command state missing: %+v", rover.LastCommand)
	}
	if rover.LastFile == nil || rover.LastFile.Filename != "image.dat" {
		t.Fatalf("file state missing: %+v", rover.LastFile)
	}
	if mission.ActiveFaultCount != 1 {
		t.Fatalf("expected one active fault, got %d", mission.ActiveFaultCount)
	}
	if rover.HealthStatus != "DEGRADED" {
		t.Fatalf("expected degraded health, got %s", rover.HealthStatus)
	}
}

func TestMissionStateClearedRecoveryReturnsNominal(t *testing.T) {
	router := NewRecordRouter()
	active := sampleRecord("EV-A", 1, KindEvent)
	active.Fields = map[string]string{"event_type": "OVERHEAT", "severity": "CRITICAL", "event_class": "FAULT", "fault_code": "THERM-1", "subsystem": "thermal"}
	if err := router.Route(active); err != nil {
		t.Fatal(err)
	}
	recovery := sampleRecord("EV-R", 2, KindEvent)
	recovery.Fields = map[string]string{"event_type": "RECOVERY", "severity": "INFO", "event_class": "RECOVERY", "recovery_of": "THERM-1"}
	if err := router.Route(recovery); err != nil {
		t.Fatal(err)
	}
	mission, ok := router.Mission("TRISHULA")
	if !ok {
		t.Fatal("mission not found")
	}
	if mission.ActiveFaultCount != 0 {
		t.Fatalf("expected no active faults, got %d", mission.ActiveFaultCount)
	}
}

func TestVehicleStateAggregationProducesDeterministicSummaries(t *testing.T) {
	engine := NewMissionStateEngine()
	first := buildProcessingResult(missionTelemetryRecord("V-A", 3, "battery", "84", "%", "NOMINAL", "GOOD", "NORMAL"), "telemetry-evaluate:battery", map[string]string{
		"metric": "battery", "value": "84", "unit": "%", "quality": "0.99", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	second := first
	second.RecordID = "V-B"
	second.SourceNode = "LANDER-01"
	second.SequenceNumber = 4
	second.ProcessedAt = first.ProcessedAt.Add(time.Second)
	if err := engine.Apply(second); err != nil {
		t.Fatal(err)
	}
	if err := engine.Apply(first); err != nil {
		t.Fatal(err)
	}

	summaries := engine.VehicleSummaries("TRISHULA")
	if len(summaries) != 2 {
		t.Fatalf("expected 2 vehicle summaries, got %d", len(summaries))
	}
	if summaries[0].SourceNode != "LANDER-01" || summaries[1].SourceNode != "ROVER-01" {
		t.Fatalf("vehicle summaries are not deterministic: %+v", summaries)
	}
	if summaries[1].AggregationVersion != "v0.9.72" {
		t.Fatalf("unexpected aggregation version: %s", summaries[1].AggregationVersion)
	}
	if summaries[1].MetricCount != 1 || summaries[1].Metrics["battery"].Value != "84" {
		t.Fatalf("unexpected rover summary: %+v", summaries[1])
	}
}

func TestVehicleStateAggregationDoesNotRegressOlderVehicleState(t *testing.T) {
	engine := NewMissionStateEngine()
	newer := buildProcessingResult(missionTelemetryRecord("NEW", 20, "battery", "80", "%", "NOMINAL", "GOOD", "NORMAL"), "telemetry-evaluate:battery", map[string]string{
		"metric": "battery", "value": "80", "unit": "%", "quality": "0.99", "quality_class": "GOOD", "limit_status": "NORMAL", "operational_state": "NOMINAL",
	})
	older := buildProcessingResult(missionTelemetryRecord("OLD", 19, "battery", "60", "%", "LOW_POWER", "DEGRADED", "LOW"), "telemetry-evaluate:battery", map[string]string{
		"metric": "battery", "value": "60", "unit": "%", "quality": "0.84", "quality_class": "DEGRADED", "limit_status": "LOW", "operational_state": "LOW_POWER",
	})
	if err := engine.Apply(newer); err != nil {
		t.Fatal(err)
	}
	if err := engine.Apply(older); err != nil {
		t.Fatal(err)
	}
	vehicle, ok := engine.Vehicle("TRISHULA", "ROVER-01")
	if !ok {
		t.Fatal("vehicle summary not found")
	}
	if vehicle.LastSequence != 20 || vehicle.Metrics["battery"].Value != "80" || vehicle.HealthStatus != "NOMINAL" {
		t.Fatalf("vehicle state regressed: %+v", vehicle)
	}
}

func TestVehicleStateLookupNotFound(t *testing.T) {
	engine := NewMissionStateEngine()
	if _, ok := engine.Vehicle("TRISHULA", "ROVER-01"); ok {
		t.Fatal("expected missing vehicle state")
	}
	if got := engine.VehicleSummaries("TRISHULA"); len(got) != 0 {
		t.Fatalf("expected empty summaries, got %+v", got)
	}
}
