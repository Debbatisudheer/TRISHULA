package main

import (
	"fmt"
	"strings"
	"time"
)

const currentCommandDispatchVersion = "v0.9.82"

// CommandDispatchRequest is the operator-facing request used to create a
// ground command record. The request intentionally does not execute the
// vehicle command; it creates the validated lifecycle entry that travels
// through the existing ground command path.
type CommandDispatchRequest struct {
	CommandID      string `json:"command_id"`
	Command        string `json:"command"`
	Target         string `json:"target"`
	Priority       string `json:"priority,omitempty"`
	Reason         string `json:"reason,omitempty"`
	Parameters     string `json:"parameters,omitempty"`
	SequenceNumber uint64 `json:"sequence_number"`
	SourceNode     string `json:"source_node,omitempty"`
	ApplicationID  uint16 `json:"application_id,omitempty"`
}

type CommandDispatchResult struct {
	Accepted        bool             `json:"accepted"`
	CommandID       string           `json:"command_id"`
	MissionID       string           `json:"mission_id"`
	Lifecycle       CommandLifecycle `json:"lifecycle"`
	Record          GroundRecord     `json:"record"`
	Ingest          IngestResult     `json:"ingest"`
	DispatchVersion string           `json:"dispatch_version"`
}

// MissionCommandDispatcher is the application boundary between an operator
// command request and the existing ground-record ingestion path.
type MissionCommandDispatcher struct {
	sourceNode string
	service    *Service
}

func NewMissionCommandDispatcher(service *Service, sourceNode string) *MissionCommandDispatcher {
	sourceNode = strings.TrimSpace(sourceNode)
	if sourceNode == "" {
		sourceNode = "GROUND-OPS-01"
	}
	return &MissionCommandDispatcher{service: service, sourceNode: sourceNode}
}

func (d *MissionCommandDispatcher) Submit(missionID string, request CommandDispatchRequest) (CommandDispatchResult, error) {
	missionID = strings.TrimSpace(missionID)
	if missionID == "" {
		return CommandDispatchResult{}, fmt.Errorf("missionID is required")
	}
	if d == nil || d.service == nil {
		return CommandDispatchResult{}, fmt.Errorf("command dispatcher is not configured")
	}

	commandID := strings.TrimSpace(request.CommandID)
	if commandID == "" {
		return CommandDispatchResult{}, fmt.Errorf("command_id is required")
	}
	command := strings.ToUpper(strings.TrimSpace(request.Command))
	if command == "" {
		return CommandDispatchResult{}, fmt.Errorf("command is required")
	}
	target := strings.TrimSpace(request.Target)
	if target == "" {
		return CommandDispatchResult{}, fmt.Errorf("target is required")
	}
	if request.SequenceNumber == 0 {
		return CommandDispatchResult{}, fmt.Errorf("sequence_number must be positive")
	}

	priority := strings.TrimSpace(request.Priority)
	if priority == "" {
		priority = "normal"
	}
	sourceNode := strings.TrimSpace(request.SourceNode)
	if sourceNode == "" {
		sourceNode = d.sourceNode
	}
	applicationID := request.ApplicationID
	if applicationID == 0 {
		applicationID = 401
	}

	record := GroundRecord{
		Envelope: GroundEnvelope{
			SchemaVersion:    1,
			Kind:             KindCommand,
			RecordID:         commandID,
			MissionID:        missionID,
			SourceNode:       sourceNode,
			OriginNode:       sourceNode,
			DestinationNode:  target,
			Priority:         priority,
			ApplicationID:    applicationID,
			MissionTimestamp: uint64(time.Now().UTC().UnixNano()),
			SequenceNumber:   request.SequenceNumber,
			Quality:          1.0,
			CorrelationID:    commandID,
			PayloadSchema:    "command.v1",
		},
		Fields: map[string]string{
			"command_id": commandID,
			"command":    command,
			"target":     target,
			"lifecycle":  string(CommandReceived),
		},
	}
	if reason := strings.TrimSpace(request.Reason); reason != "" {
		record.Fields["reason"] = reason
	}
	if parameters := strings.TrimSpace(request.Parameters); parameters != "" {
		record.Fields["parameters"] = parameters
	}

	if err := validateRecord(record); err != nil {
		return CommandDispatchResult{}, err
	}

	ingest := d.service.Ingest(record)
	if !ingest.Accepted {
		return CommandDispatchResult{
			Accepted:        false,
			CommandID:       commandID,
			MissionID:       missionID,
			Lifecycle:       CommandReceived,
			Record:          record,
			Ingest:          ingest,
			DispatchVersion: currentCommandDispatchVersion,
		}, nil
	}

	return CommandDispatchResult{
		Accepted:        true,
		CommandID:       commandID,
		MissionID:       missionID,
		Lifecycle:       CommandReceived,
		Record:          record,
		Ingest:          ingest,
		DispatchVersion: currentCommandDispatchVersion,
	}, nil
}
