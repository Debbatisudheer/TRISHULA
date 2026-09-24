package main

import (
	"errors"
	"testing"
)

type recordingHandler struct{ count int }

func (h *recordingHandler) Handle(GroundRecord) error { h.count++; return nil }

func (h *recordingHandler) HandleOperation(record GroundRecord) (string, error) {
	h.count++
	return "recorded", nil
}

func (h *recordingHandler) ProcessOutput(record GroundRecord) (ProcessingResult, error) {
	h.count++
	return buildProcessingResult(record, "recorded", map[string]string{"test": "ok"}), nil
}

type failingHandler struct{}

func (failingHandler) Handle(GroundRecord) error { return errors.New("pipeline failed") }
func (failingHandler) HandleOperation(GroundRecord) (string, error) {
	return "", errors.New("pipeline failed")
}
func (failingHandler) ProcessOutput(GroundRecord) (ProcessingResult, error) {
	return ProcessingResult{}, errors.New("pipeline failed")
}

func TestRecordRouterRoutesByKind(t *testing.T) {
	r := NewRecordRouter()
	telemetry := sampleRecord("T-1", 1, KindTelemetry)
	telemetry.Fields = map[string]string{"metric": "battery", "value": "90"}
	science := sampleRecord("S-1", 2, KindScience)
	science.Fields = map[string]string{"instrument": "LIBS", "target": "TARGET-7"}
	event := sampleRecord("E-1", 3, KindEvent)
	event.Fields = map[string]string{"event_type": "THERMAL_WARNING", "severity": "WARNING"}
	command := sampleRecord("C-1", 4, KindCommand)
	command.Fields = map[string]string{"command": "STOP_ROVER", "target": "ROVER-01"}
	file := sampleRecord("F-1", 5, KindFile)
	file.Fields = map[string]string{"filename": "image.dat", "size_bytes": "4096"}

	for _, record := range []GroundRecord{telemetry, science, event, command, file} {
		if err := r.Route(record); err != nil {
			t.Fatal(err)
		}
	}
	got := r.Snapshot()
	if got.Telemetry.Accepted != 1 || got.Science.Accepted != 1 || got.Event.Accepted != 1 || got.Command.Accepted != 1 || got.File.Accepted != 1 {
		t.Fatalf("unexpected route snapshot: %+v", got)
	}
	if got.Science.LastOperation != "science-evaluate:LIBS:TARGET-7" {
		t.Fatalf("unexpected science operation: %+v", got.Science)
	}
	if got.Science.LastResult == nil || got.Science.LastResult.Attributes["instrument"] != "LIBS" {
		t.Fatalf("missing structured science result: %+v", got.Science.LastResult)
	}
}

func TestDomainHandlersRejectMalformedPayloads(t *testing.T) {
	cases := []struct {
		name   string
		kind   DataKind
		fields map[string]string
	}{
		{"science-missing-instrument", KindScience, map[string]string{"target": "TARGET-7"}},
		{"event-missing-severity", KindEvent, map[string]string{"event_type": "FAULT"}},
		{"command-missing-target", KindCommand, map[string]string{"command": "STOP_ROVER"}},
		{"file-invalid-size", KindFile, map[string]string{"filename": "a.bin", "size_bytes": "NaN"}},
		{"telemetry-empty", KindTelemetry, map[string]string{}},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			record := sampleRecord(tc.name, 1, tc.kind)
			record.Fields = tc.fields
			if err := NewRecordRouter().Route(record); err == nil {
				t.Fatal("expected domain validation error")
			}
		})
	}
}

func TestRoutedPipelineTracksFailure(t *testing.T) {
	p := NewRoutedPipeline(failingHandler{})
	if err := p.Process(sampleRecord("T-1", 1, KindTelemetry)); err == nil {
		t.Fatal("expected pipeline failure")
	}
	s := p.Snapshot()
	if s.Received != 1 || s.Rejected != 1 || s.Accepted != 0 || s.LastError == "" {
		t.Fatalf("unexpected snapshot: %+v", s)
	}
}

func TestRoutedPipelineTracksHandler(t *testing.T) {
	h := &recordingHandler{}
	p := NewRoutedPipeline(h)
	if err := p.Process(sampleRecord("T-1", 1, KindTelemetry)); err != nil {
		t.Fatal(err)
	}
	if h.count != 1 {
		t.Fatalf("handler count = %d", h.count)
	}
	s := p.Snapshot()
	if s.Received != 1 || s.Accepted != 1 || s.LastRecordID != "T-1" || s.LastSequence != 1 {
		t.Fatalf("unexpected snapshot: %+v", s)
	}
}

func TestDomainProcessingOutputs(t *testing.T) {
	r := NewRecordRouter()
	records := []GroundRecord{
		sampleRecord("OUT-T", 12, KindTelemetry),
		sampleRecord("OUT-S", 13, KindScience),
		sampleRecord("OUT-E", 14, KindEvent),
		sampleRecord("OUT-C", 15, KindCommand),
		sampleRecord("OUT-F", 16, KindFile),
	}
	records[0].Fields = map[string]string{"metric": "battery", "value": "85.4"}
	records[1].Fields = map[string]string{"instrument": "LIBS", "target": "TARGET-7", "measurement": "elemental_abundance"}
	records[2].Fields = map[string]string{"event_type": "THERMAL_WARNING", "severity": "WARNING", "subsystem": "thermal"}
	records[3].Fields = map[string]string{"command": "STOP_ROVER", "target": "ROVER-01", "reason": "test", "command_id": "CMD-OUT-1", "lifecycle": "VALIDATED"}
	records[4].Fields = map[string]string{"filename": "image.dat", "media_type": "science-data", "size_bytes": "4096"}
	for _, record := range records {
		if err := r.Route(record); err != nil {
			t.Fatalf("route %s: %v", record.Envelope.RecordID, err)
		}
	}
	results := r.Results()
	if results[KindTelemetry].Attributes["metric"] != "battery" || results[KindTelemetry].Attributes["value"] != "85.4" {
		t.Fatalf("unexpected telemetry output: %+v", results[KindTelemetry])
	}
	if results[KindScience].Attributes["measurement"] != "elemental_abundance" {
		t.Fatalf("unexpected science output: %+v", results[KindScience])
	}
	if results[KindEvent].Attributes["severity"] != "WARNING" {
		t.Fatalf("unexpected event output: %+v", results[KindEvent])
	}
	if results[KindCommand].Attributes["command"] != "STOP_ROVER" || results[KindCommand].Attributes["lifecycle"] != "VALIDATED" {
		t.Fatalf("unexpected command output: %+v", results[KindCommand])
	}
	if results[KindFile].Attributes["size_bytes"] != "4096" {
		t.Fatalf("unexpected file output: %+v", results[KindFile])
	}
	if results[KindTelemetry].ProcessorVersion != currentProcessorVersion {
		t.Fatalf("unexpected processor version: %+v", results[KindTelemetry])
	}
}

func TestRecordRouterCommandLifecycle(t *testing.T) {
	r := NewRecordRouter()
	states := []string{"VALIDATED", "SENT", "ACKNOWLEDGED", "EXECUTING", "COMPLETED"}
	for i, state := range states {
		record := sampleRecord("CMD-LIFE-"+string(rune('A'+i)), uint64(30+i), KindCommand)
		record.Fields = map[string]string{
			"command_id": "CMD-LIFECYCLE-1",
			"command":    "STOP_ROVER",
			"target":     "ROVER-01",
			"lifecycle":  state,
		}
		if err := r.Route(record); err != nil {
			t.Fatalf("route %s: %v", state, err)
		}
	}
	statesOut := r.CommandLifecycle()
	if len(statesOut) != 1 {
		t.Fatalf("expected one command lifecycle state, got %d", len(statesOut))
	}
	if statesOut[0].Lifecycle != CommandCompleted || !statesOut[0].Terminal {
		t.Fatalf("unexpected final command lifecycle: %+v", statesOut[0])
	}
	result := r.Results()[KindCommand]
	if result.Attributes["transition"] != "EXECUTING->COMPLETED" {
		t.Fatalf("unexpected command transition: %+v", result.Attributes)
	}
}
