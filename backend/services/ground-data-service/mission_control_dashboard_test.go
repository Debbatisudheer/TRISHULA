package main

import (
	"testing"
	"time"
)

func TestBuildMissionControlDashboardEmpty(t *testing.T) {
	got := buildMissionControlDashboard(nil, time.Unix(0, 0), "req-104")
	if got.Version != "v0.9.104" || got.Status != "degraded" || got.Missions == nil {
		t.Fatalf("dashboard=%+v", got)
	}
}

func TestBuildMissionControlDashboardAggregatesMissionStateAlertsAndTelemetry(t *testing.T) {
	consumer := &KafkaConsumer{router: NewRecordRouter(), alertManager: NewMissionControlAlertManager()}
	telemetry := GroundRecord{
		Envelope: GroundEnvelope{SchemaVersion: 1, Kind: KindTelemetry, RecordID: "DASH-105-TEL", MissionID: "TRISHULA", SourceNode: "ROVER-01", OriginNode: "ROVER-01", DestinationNode: "GROUND-OPS-01", Priority: "high", ApplicationID: 901, MissionTimestamp: 2000000, SequenceNumber: 900101, Quality: 1, CorrelationID: "DASH-105-TEL", PayloadSchema: "trishula.telemetry.v1"},
		Fields:   map[string]string{"metric": "dashboard_hardening", "value": "0.95", "unit": "ratio"},
	}
	if err := consumer.router.Route(telemetry); err != nil {
		t.Fatalf("route telemetry: %v", err)
	}
	alertRecord := alertEventRecord("DASH-105-ALERT", "ENGINE_FAULT", "ERROR", "ACTIVE", "ENGINE_FAULT")
	if _, ok := consumer.alertManager.ProcessEvent(alertRecord); !ok {
		t.Fatal("expected alert")
	}

	got := buildMissionControlDashboard(consumer, time.Date(2026, 9, 18, 12, 0, 0, 0, time.UTC), "dash-105")
	if got.Version != "v0.9.104" || got.MissionCount != 1 {
		t.Fatalf("dashboard=%+v", got)
	}
	if got.Summary.VehicleCount != 1 || got.Summary.ActiveAlertCount != 1 || got.Summary.TotalAlertCount != 1 || got.Summary.TelemetryAccepted != 1 {
		t.Fatalf("summary=%+v", got.Summary)
	}
	if len(got.Missions) != 1 || got.Missions[0].MissionID != "TRISHULA" {
		t.Fatalf("missions=%+v", got.Missions)
	}
	mission := got.Missions[0]
	if mission.VehicleCount != 1 || mission.ActiveAlertCount != 1 || mission.TotalAlertCount != 1 || len(mission.Vehicles) != 1 {
		t.Fatalf("mission=%+v", mission)
	}
	vehicle := mission.Vehicles[0]
	if vehicle.SourceNode != "ROVER-01" || vehicle.LastSequence != 900101 || vehicle.MetricCount != 1 || vehicle.SubsystemCount != 1 {
		t.Fatalf("vehicle=%+v", vehicle)
	}
}

func TestBuildMissionControlDashboardOrdersMissionsAndVehiclesDeterministically(t *testing.T) {
	consumer := &KafkaConsumer{router: NewRecordRouter(), alertManager: NewMissionControlAlertManager()}
	for _, record := range []GroundRecord{
		{Envelope: GroundEnvelope{Kind: KindTelemetry, RecordID: "DASH-105-B", MissionID: "MISSION-B", SourceNode: "ROVER-Z", SequenceNumber: 2, MissionTimestamp: 2}, Fields: map[string]string{"metric": "b", "value": "2"}},
		{Envelope: GroundEnvelope{Kind: KindTelemetry, RecordID: "DASH-105-A2", MissionID: "MISSION-A", SourceNode: "ROVER-Z", SequenceNumber: 2, MissionTimestamp: 2}, Fields: map[string]string{"metric": "a2", "value": "2"}},
		{Envelope: GroundEnvelope{Kind: KindTelemetry, RecordID: "DASH-105-A1", MissionID: "MISSION-A", SourceNode: "ROVER-A", SequenceNumber: 1, MissionTimestamp: 1}, Fields: map[string]string{"metric": "a1", "value": "1"}},
	} {
		if err := consumer.router.Route(record); err != nil {
			t.Fatalf("route %s: %v", record.Envelope.RecordID, err)
		}
	}
	got := buildMissionControlDashboard(consumer, time.Now().UTC(), "dash-105-order")
	if len(got.Missions) != 2 || got.Missions[0].MissionID != "MISSION-A" || got.Missions[1].MissionID != "MISSION-B" {
		t.Fatalf("mission order=%+v", got.Missions)
	}
	vehicles := got.Missions[0].Vehicles
	if len(vehicles) != 2 || vehicles[0].SourceNode != "ROVER-A" || vehicles[1].SourceNode != "ROVER-Z" {
		t.Fatalf("vehicle order=%+v", vehicles)
	}
}
