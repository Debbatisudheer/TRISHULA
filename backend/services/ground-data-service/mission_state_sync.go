package main

import (
	"context"
	"fmt"
	"sort"
	"strings"
	"time"
)

const currentMissionStateSynchronizationVersion = "v0.9.78"

// MissionStateSynchronization describes a durable-history-driven rebuild of
// one mission's in-memory operational projection. The durable repository is
// treated as authoritative; a successful synchronization atomically replaces
// only the requested mission projection.
type MissionStateSynchronization struct {
	MissionID                          string            `json:"mission_id"`
	Status                             string            `json:"status"`
	SynchronizedAt                     time.Time         `json:"synchronized_at"`
	DurableRecordCount                 int               `json:"durable_record_count"`
	ProcessedRecordCount               int               `json:"processed_record_count"`
	PreviousStateFound                 bool              `json:"previous_state_found"`
	PreviousLatestSequenceBySource     map[string]uint64 `json:"previous_latest_sequence_by_source,omitempty"`
	SynchronizedLatestSequenceBySource map[string]uint64 `json:"synchronized_latest_sequence_by_source,omitempty"`
	SynchronizedVehicleCount           int               `json:"synchronized_vehicle_count"`
	FailedRecordID                     string            `json:"failed_record_id,omitempty"`
	FailureReason                      string            `json:"failure_reason,omitempty"`
	SynchronizationVersion             string            `json:"synchronization_version"`
}

// SynchronizeMissionState rebuilds one mission from durable history using a
// fresh processing/router instance, then atomically swaps the rebuilt mission
// projection into the live engine. The existing live state is left untouched
// if replay of any durable record fails.
func (r *RecordRouter) SynchronizeMissionState(ctx context.Context, repo RecordRepository, missionID string, synchronizedAt time.Time) (MissionStateSynchronization, bool, error) {
	if r == nil || r.missionState == nil || repo == nil {
		return MissionStateSynchronization{}, false, nil
	}
	missionID = strings.TrimSpace(missionID)
	if missionID == "" {
		return MissionStateSynchronization{}, false, fmt.Errorf("missionID is required")
	}
	if synchronizedAt.IsZero() {
		synchronizedAt = time.Now().UTC()
	} else {
		synchronizedAt = synchronizedAt.UTC()
	}

	records, err := repo.Query(ctx, RecordQuery{MissionID: missionID})
	if err != nil {
		return MissionStateSynchronization{}, false, err
	}
	if len(records) == 0 {
		return MissionStateSynchronization{
			MissionID:                missionID,
			Status:                   "NO_DATA",
			SynchronizedAt:           synchronizedAt,
			DurableRecordCount:       0,
			ProcessedRecordCount:     0,
			SynchronizedVehicleCount: 0,
			SynchronizationVersion:   currentMissionStateSynchronizationVersion,
		}, false, nil
	}

	// Rebuild in deterministic mission-time/sequence order so replay produces
	// stable operational state even if the underlying repository returns records
	// in a different order.
	sort.SliceStable(records, func(i, j int) bool {
		a, b := records[i].Envelope, records[j].Envelope
		if a.MissionTimestamp != b.MissionTimestamp {
			return a.MissionTimestamp < b.MissionTimestamp
		}
		if a.SequenceNumber != b.SequenceNumber {
			return a.SequenceNumber < b.SequenceNumber
		}
		if a.RecordID != b.RecordID {
			return a.RecordID < b.RecordID
		}
		return a.Kind < b.Kind
	})

	// Snapshot current state before replacement for operator diagnostics.
	previous, previousFound := r.missionState.Get(missionID)
	previousSeq := map[string]uint64{}
	if previousFound {
		previousSeq = missionSequenceMap(previous)
	}

	replayRouter := NewRecordRouter()
	processed := 0
	for _, record := range records {
		var replayErr error
		if record.Envelope.Kind == KindCommand {
			// Durable command history stores lifecycle snapshots. During a
			// mission-state rebuild we must restore that persisted snapshot
			// directly rather than replaying it through the live transition
			// validator (which intentionally rejects an initial COMPLETED
			// transition). The live command path remains unchanged.
			var result ProcessingResult
			result, replayErr = processCommandSnapshotOutput(record)
			if replayErr == nil {
				replayErr = replayRouter.missionState.Apply(result)
			}
		} else {
			replayErr = replayRouter.Route(record)
		}
		if replayErr != nil {
			return MissionStateSynchronization{
				MissionID:                      missionID,
				Status:                         "FAILED",
				SynchronizedAt:                 synchronizedAt,
				DurableRecordCount:             len(records),
				ProcessedRecordCount:           processed,
				PreviousStateFound:             previousFound,
				PreviousLatestSequenceBySource: previousSeq,
				FailedRecordID:                 record.Envelope.RecordID,
				FailureReason:                  replayErr.Error(),
				SynchronizationVersion:         currentMissionStateSynchronizationVersion,
			}, false, replayErr
		}
		processed++
	}

	rebuilt, found := replayRouter.Mission(missionID)
	if !found {
		return MissionStateSynchronization{}, false, fmt.Errorf("replay completed without mission state")
	}
	newSeq := missionSequenceMap(rebuilt)

	r.missionState.ReplaceMission(missionID, rebuilt)
	return MissionStateSynchronization{
		MissionID:                          missionID,
		Status:                             "SYNCHRONIZED",
		SynchronizedAt:                     synchronizedAt,
		DurableRecordCount:                 len(records),
		ProcessedRecordCount:               processed,
		PreviousStateFound:                 previousFound,
		PreviousLatestSequenceBySource:     previousSeq,
		SynchronizedLatestSequenceBySource: newSeq,
		SynchronizedVehicleCount:           len(rebuilt.Vehicles),
		SynchronizationVersion:             currentMissionStateSynchronizationVersion,
	}, true, nil
}

func missionSequenceMap(mission MissionState) map[string]uint64 {
	out := make(map[string]uint64, len(mission.Vehicles))
	for source, vehicle := range mission.Vehicles {
		out[source] = vehicle.LastSequence
	}
	return out
}
