package main

import (
	"fmt"
	"strconv"
	"strings"
	"time"
)

// ProcessingResult is the stable, structured output of a domain processor.
// It is intentionally independent of the incoming Kafka envelope so that
// later milestones can persist, publish, or route processing results without
// coupling downstream consumers to handler implementation details.
type ProcessingResult struct {
	RecordID           string            `json:"record_id"`
	Kind               DataKind          `json:"kind"`
	MissionID          string            `json:"mission_id"`
	SourceNode         string            `json:"source_node"`
	SequenceNumber     uint64            `json:"sequence_number"`
	MissionTimestampNS uint64            `json:"mission_timestamp_ns"`
	Status             string            `json:"status"`
	Operation          string            `json:"operation"`
	ProcessorVersion   string            `json:"processor_version"`
	ProcessedAt        time.Time         `json:"processed_at"`
	Attributes         map[string]string `json:"attributes"`
}

const currentProcessorVersion = "v0.9.71"

func buildProcessingResult(record GroundRecord, operation string, attributes map[string]string) ProcessingResult {
	copied := make(map[string]string, len(attributes))
	for key, value := range attributes {
		copied[key] = value
	}
	return ProcessingResult{
		RecordID:           record.Envelope.RecordID,
		Kind:               record.Envelope.Kind,
		MissionID:          record.Envelope.MissionID,
		SourceNode:         record.Envelope.SourceNode,
		SequenceNumber:     record.Envelope.SequenceNumber,
		MissionTimestampNS: record.Envelope.MissionTimestamp,
		Status:             "accepted",
		Operation:          operation,
		ProcessorVersion:   currentProcessorVersion,
		ProcessedAt:        time.Now().UTC(),
		Attributes:         copied,
	}
}

func processTelemetryOutput(record GroundRecord) (ProcessingResult, error) {
	return processTelemetryEngineOutput(record)
}

func processScienceOutput(record GroundRecord) (ProcessingResult, error) {
	return processScienceEngineOutput(record)
}

func processEventOutput(record GroundRecord) (ProcessingResult, error) {
	return processEventEngineOutput(record, defaultEventEngine)
}

func processCommandOutput(record GroundRecord) (ProcessingResult, error) {
	return processCommandEngineOutput(record, defaultCommandEngine)
}

func processFileOutput(record GroundRecord) (ProcessingResult, error) {
	operation, err := processFileRecord(record)
	if err != nil {
		return ProcessingResult{}, err
	}
	filename := strings.TrimSpace(record.Fields["filename"])
	attributes := map[string]string{"filename": filename}
	for _, key := range []string{"media_type", "checksum", "compression"} {
		if value := strings.TrimSpace(record.Fields[key]); value != "" {
			attributes[key] = value
		}
	}
	if sizeRaw := strings.TrimSpace(record.Fields["size_bytes"]); sizeRaw != "" {
		size, parseErr := strconv.ParseUint(sizeRaw, 10, 64)
		if parseErr != nil {
			return ProcessingResult{}, fmt.Errorf("invalid size_bytes: %w", parseErr)
		}
		attributes["size_bytes"] = strconv.FormatUint(size, 10)
	}
	return buildProcessingResult(record, operation, attributes), nil
}
