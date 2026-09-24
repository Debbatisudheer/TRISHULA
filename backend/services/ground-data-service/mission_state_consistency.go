package main

import (
	"context"
	"sort"
	"strings"
	"time"
)

const currentMissionStateConsistencyVersion = "v0.9.77"

type SequenceMismatch struct {
	SourceNode      string `json:"source_node"`
	DurableSequence uint64 `json:"durable_sequence"`
	StateSequence   uint64 `json:"state_sequence"`
}

// MissionStateConsistency compares the derived in-process mission projection
// with the durable ground-record history. It is a read-only drift check; it
// never replaces PostgreSQL history or mutates the operational state.
type MissionStateConsistency struct {
	MissionID                     string             `json:"mission_id"`
	Status                        string             `json:"status"`
	CheckedAt                     time.Time          `json:"checked_at"`
	DurableRecordCount            int                `json:"durable_record_count"`
	DurableSourceCount            int                `json:"durable_source_count"`
	StateVehicleCount             int                `json:"state_vehicle_count"`
	DurableLatestSequenceBySource map[string]uint64  `json:"durable_latest_sequence_by_source"`
	StateLastSequenceBySource     map[string]uint64  `json:"state_last_sequence_by_source"`
	MissingInState                []string           `json:"missing_in_state,omitempty"`
	MissingInDurable              []string           `json:"missing_in_durable,omitempty"`
	SequenceMismatches            []SequenceMismatch `json:"sequence_mismatches,omitempty"`
	ReconciliationRequired        bool               `json:"reconciliation_required"`
	ConsistencyVersion            string             `json:"consistency_version"`
}

// MissionStateConsistency builds a deterministic comparison between durable
// records and the in-process mission-state projection.
func (e *MissionStateEngine) CheckConsistency(ctx context.Context, repo RecordRepository, missionID string, checkedAt time.Time) (MissionStateConsistency, bool, error) {
	if e == nil || repo == nil {
		return MissionStateConsistency{}, false, nil
	}
	missionID = strings.TrimSpace(missionID)
	if missionID == "" {
		return MissionStateConsistency{}, false, nil
	}
	if checkedAt.IsZero() {
		checkedAt = time.Now().UTC()
	} else {
		checkedAt = checkedAt.UTC()
	}

	records, err := repo.Query(ctx, RecordQuery{MissionID: missionID})
	if err != nil {
		return MissionStateConsistency{}, false, err
	}

	durableSeq := make(map[string]uint64)
	for _, record := range records {
		source := strings.TrimSpace(record.Envelope.SourceNode)
		if source == "" {
			continue
		}
		if current, ok := durableSeq[source]; !ok || record.Envelope.SequenceNumber > current {
			durableSeq[source] = record.Envelope.SequenceNumber
		}
	}

	e.mu.RLock()
	mission, stateFound := e.missions[missionID]
	if stateFound {
		mission = cloneMissionState(mission)
	}
	e.mu.RUnlock()

	stateSeq := make(map[string]uint64)
	if stateFound {
		for source, vehicle := range mission.Vehicles {
			stateSeq[source] = vehicle.LastSequence
		}
	}

	result := MissionStateConsistency{
		MissionID:                     missionID,
		CheckedAt:                     checkedAt,
		DurableRecordCount:            len(records),
		DurableSourceCount:            len(durableSeq),
		StateVehicleCount:             len(stateSeq),
		DurableLatestSequenceBySource: copyUint64Map(durableSeq),
		StateLastSequenceBySource:     copyUint64Map(stateSeq),
		ConsistencyVersion:            currentMissionStateConsistencyVersion,
	}

	missingInState := make([]string, 0)
	missingInDurable := make([]string, 0)
	mismatches := make([]SequenceMismatch, 0)

	for source, durableSequence := range durableSeq {
		stateSequence, ok := stateSeq[source]
		if !ok {
			missingInState = append(missingInState, source)
			continue
		}
		if stateSequence != durableSequence {
			mismatches = append(mismatches, SequenceMismatch{
				SourceNode:      source,
				DurableSequence: durableSequence,
				StateSequence:   stateSequence,
			})
		}
	}
	for source := range stateSeq {
		if _, ok := durableSeq[source]; !ok {
			missingInDurable = append(missingInDurable, source)
		}
	}

	sort.Strings(missingInState)
	sort.Strings(missingInDurable)
	sort.Slice(mismatches, func(i, j int) bool { return mismatches[i].SourceNode < mismatches[j].SourceNode })
	result.MissingInState = missingInState
	result.MissingInDurable = missingInDurable
	result.SequenceMismatches = mismatches

	switch {
	case len(records) == 0 && !stateFound:
		result.Status = "NO_DATA"
	case len(missingInState) > 0 || len(mismatches) > 0:
		result.Status = "DIVERGED"
		result.ReconciliationRequired = true
	case len(missingInDurable) > 0:
		result.Status = "STATE_AHEAD"
		result.ReconciliationRequired = true
	default:
		result.Status = "IN_SYNC"
	}

	return result, true, nil
}

func copyUint64Map(input map[string]uint64) map[string]uint64 {
	out := make(map[string]uint64, len(input))
	for key, value := range input {
		out[key] = value
	}
	return out
}
