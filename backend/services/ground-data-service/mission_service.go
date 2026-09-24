package main

import (
	"context"
	"fmt"
	"sort"
	"strings"
	"time"
)

const currentMissionServiceVersion = "v0.9.79"

// MissionService is the mission-level application boundary over the existing
// mission state, health, freshness, consistency, and synchronization engines.
// It intentionally does not own durable storage; the repository remains the
// source of durable history and RecordRouter remains the state projection owner.
type MissionService struct {
	router *RecordRouter
	repo   RecordRepository
}

// MissionSummary is a compact operator-facing mission listing.
type MissionSummary struct {
	MissionID             string    `json:"mission_id"`
	UpdatedAt             time.Time `json:"updated_at"`
	VehicleCount          int       `json:"vehicle_count"`
	ActiveFaultCount      uint64    `json:"active_fault_count"`
	HighestFaultSeverity  string    `json:"highest_fault_severity,omitempty"`
	HealthStatus          string    `json:"health_status"`
	Readiness             string    `json:"readiness"`
	FreshnessStatus       string    `json:"freshness_status"`
	MissionServiceVersion string    `json:"mission_service_version"`
}

// MissionDetail is the application-level mission view exposed by the Mission
// Service. It aggregates projections that already exist elsewhere in the
// ground-data service instead of duplicating their state logic.
type MissionDetail struct {
	State          MissionState             `json:"state"`
	Health         MissionHealthSummary     `json:"health"`
	Freshness      MissionFreshnessSummary  `json:"freshness"`
	Consistency    *MissionStateConsistency `json:"consistency,omitempty"`
	ServiceVersion string                   `json:"mission_service_version"`
}

func NewMissionService(router *RecordRouter, repo RecordRepository) *MissionService {
	return &MissionService{router: router, repo: repo}
}

func (s *MissionService) List(ctx context.Context, now time.Time) []MissionSummary {
	if s == nil || s.router == nil {
		return []MissionSummary{}
	}
	if now.IsZero() {
		now = time.Now().UTC()
	} else {
		now = now.UTC()
	}

	states := s.router.MissionState()
	out := make([]MissionSummary, 0, len(states))
	for _, state := range states {
		summary := MissionSummary{
			MissionID:             state.MissionID,
			UpdatedAt:             state.UpdatedAt,
			VehicleCount:          len(state.Vehicles),
			ActiveFaultCount:      state.ActiveFaultCount,
			HighestFaultSeverity:  strings.ToUpper(strings.TrimSpace(state.HighestFaultSeverity)),
			MissionServiceVersion: currentMissionServiceVersion,
		}
		if health, found := s.router.MissionHealth(state.MissionID); found {
			summary.HealthStatus = health.HealthStatus
			summary.Readiness = health.Readiness
		} else {
			summary.HealthStatus = "UNKNOWN"
			summary.Readiness = "UNKNOWN"
		}
		if freshness, found, err := s.router.MissionFreshness(state.MissionID, now, defaultMissionFreshnessWarn, defaultMissionFreshnessCritical); err == nil && found {
			summary.FreshnessStatus = freshness.FreshnessStatus
		} else {
			summary.FreshnessStatus = "UNKNOWN"
		}
		out = append(out, summary)
	}

	sort.Slice(out, func(i, j int) bool {
		return out[i].MissionID < out[j].MissionID
	})
	return out
}

func (s *MissionService) Get(ctx context.Context, missionID string, now time.Time) (MissionDetail, bool, error) {
	if s == nil || s.router == nil {
		return MissionDetail{}, false, nil
	}
	missionID = strings.TrimSpace(missionID)
	if missionID == "" {
		return MissionDetail{}, false, fmt.Errorf("missionID is required")
	}
	if now.IsZero() {
		now = time.Now().UTC()
	} else {
		now = now.UTC()
	}

	state, found := s.router.Mission(missionID)
	if !found {
		return MissionDetail{}, false, nil
	}

	health, healthFound := s.router.MissionHealth(missionID)
	if !healthFound {
		health = MissionHealthSummary{MissionID: missionID, HealthStatus: "UNKNOWN", Readiness: "UNKNOWN"}
	}
	freshness, freshnessFound, err := s.router.MissionFreshness(missionID, now, defaultMissionFreshnessWarn, defaultMissionFreshnessCritical)
	if err != nil {
		return MissionDetail{}, false, err
	}
	if !freshnessFound {
		freshness = MissionFreshnessSummary{MissionID: missionID, FreshnessStatus: "UNKNOWN", ReadinessImpact: "UNKNOWN", AsOf: now, FreshnessVersion: currentMissionFreshnessVersion}
	}

	detail := MissionDetail{
		State:          state,
		Health:         health,
		Freshness:      freshness,
		ServiceVersion: currentMissionServiceVersion,
	}
	if s.repo != nil {
		consistency, consistencyFound, consistencyErr := s.router.MissionStateConsistency(ctx, s.repo, missionID, now)
		if consistencyErr != nil {
			return MissionDetail{}, false, consistencyErr
		}
		if consistencyFound {
			detail.Consistency = &consistency
		}
	}
	return detail, true, nil
}

func (s *MissionService) Synchronize(ctx context.Context, missionID string, synchronizedAt time.Time) (MissionStateSynchronization, bool, error) {
	if s == nil || s.router == nil || s.repo == nil {
		return MissionStateSynchronization{}, false, fmt.Errorf("mission service synchronization is not configured")
	}
	return s.router.SynchronizeMissionState(ctx, s.repo, missionID, synchronizedAt)
}
