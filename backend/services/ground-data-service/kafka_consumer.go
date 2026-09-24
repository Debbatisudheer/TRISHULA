package main

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"sync"
	"time"

	"github.com/segmentio/kafka-go"
)

type ConsumerSnapshot struct {
	Enabled       bool      `json:"enabled"`
	GroupID       string    `json:"group_id"`
	Topic         string    `json:"topic"`
	Broker        string    `json:"broker"`
	Received      uint64    `json:"received"`
	Accepted      uint64    `json:"accepted"`
	Invalid       uint64    `json:"invalid"`
	LastTopic     string    `json:"last_topic,omitempty"`
	LastPartition int       `json:"last_partition,omitempty"`
	LastOffset    int64     `json:"last_offset,omitempty"`
	LastRecordID  string    `json:"last_record_id,omitempty"`
	LastSequence  uint64    `json:"last_sequence,omitempty"`
	LastError     string    `json:"last_error,omitempty"`
	LastMessageAt time.Time `json:"last_message_at,omitempty"`
}

type KafkaConsumer struct {
	reader         *kafka.Reader
	router         *RecordRouter
	orchestrator   *CommandExecutionOrchestrator
	orchestratorMu sync.RWMutex
	webSocketMu    sync.RWMutex
	webSocket      *MissionControlWebSocketGateway
	alertManager   *MissionControlAlertManager
	cancel         context.CancelFunc
	wg             sync.WaitGroup
	mu             sync.RWMutex
	snap           ConsumerSnapshot
}

func NewKafkaRecordConsumer(brokers, topic, groupID string) (*KafkaConsumer, error) {
	if brokers == "" || topic == "" || groupID == "" {
		return nil, fmt.Errorf("kafka consumer requires brokers, topic, and group_id")
	}
	ctx, cancel := context.WithCancel(context.Background())
	reader := kafka.NewReader(kafka.ReaderConfig{
		Brokers:        splitCSV(brokers),
		Topic:          topic,
		GroupID:        groupID,
		MinBytes:       1,
		MaxBytes:       10e6,
		MaxWait:        200 * time.Millisecond,
		CommitInterval: 0,
	})
	consumer := &KafkaConsumer{
		reader:       reader,
		router:       NewRecordRouter(),
		alertManager: NewMissionControlAlertManager(),
		cancel:       cancel,
		snap: ConsumerSnapshot{
			Enabled: true,
			GroupID: groupID,
			Topic:   topic,
			Broker:  brokers,
		},
	}
	consumer.wg.Add(1)
	go consumer.run(ctx)
	return consumer, nil
}

func (c *KafkaConsumer) run(ctx context.Context) {
	defer c.wg.Done()
	for {
		message, err := c.reader.FetchMessage(ctx)
		if err != nil {
			if ctx.Err() != nil {
				return
			}
			c.setError(err)
			continue
		}
		c.mu.Lock()
		c.snap.Received++
		c.snap.LastTopic = message.Topic
		c.snap.LastPartition = message.Partition
		c.snap.LastOffset = message.Offset
		c.snap.LastMessageAt = time.Now().UTC()
		c.mu.Unlock()

		var record GroundRecord
		if err := json.Unmarshal(message.Value, &record); err != nil {
			c.mu.Lock()
			c.snap.Invalid++
			c.snap.LastError = "invalid JSON: " + err.Error()
			c.mu.Unlock()
			// Commit the malformed message so one bad message does not block the group forever.
			_ = c.reader.CommitMessages(ctx, message)
			continue
		}
		if err := validateRecord(record); err != nil {
			c.mu.Lock()
			c.snap.Invalid++
			c.snap.LastRecordID = record.Envelope.RecordID
			c.snap.LastError = "invalid record: " + err.Error()
			c.mu.Unlock()
			_ = c.reader.CommitMessages(ctx, message)
			continue
		}
		if err := c.router.Route(record); err != nil {
			c.mu.Lock()
			c.snap.Invalid++
			c.snap.LastRecordID = record.Envelope.RecordID
			c.snap.LastSequence = record.Envelope.SequenceNumber
			c.snap.LastError = "routing failed: " + err.Error()
			c.mu.Unlock()
			_ = c.reader.CommitMessages(ctx, message)
			continue
		}
		if record.Envelope.Kind == KindCommand && strings.EqualFold(strings.TrimSpace(record.Fields["lifecycle"]), string(CommandReceived)) {
			c.orchestratorMu.RLock()
			orchestrator := c.orchestrator
			c.orchestratorMu.RUnlock()
			if orchestrator != nil {
				if err := orchestrator.Submit(record); err != nil {
					c.setError(fmt.Errorf("command orchestrator: %w", err))
				}
			}
		}
		if record.Envelope.Kind == KindTelemetry {
			c.publishTelemetry(record)
		}
		if record.Envelope.Kind == KindEvent {
			if alert, ok := c.alertManager.ProcessEvent(record); ok {
				c.publishAlert(alert)
			}
		}
		if (record.Envelope.Kind == KindCommand && strings.TrimSpace(record.Fields["lifecycle"]) != "") ||
			(record.Envelope.Kind == KindEvent && strings.EqualFold(strings.TrimSpace(record.Fields["event_class"]), "COMMAND")) {
			c.publishCommandExecution(record)
		}
		c.mu.Lock()
		c.snap.Accepted++
		c.snap.LastRecordID = record.Envelope.RecordID
		c.snap.LastSequence = record.Envelope.SequenceNumber
		c.snap.LastError = ""
		c.mu.Unlock()
		if err := c.reader.CommitMessages(ctx, message); err != nil {
			c.setError(err)
		}
	}
}

// AttachWebSocketGateway connects accepted Kafka telemetry to the Mission
// Control real-time stream. It is intentionally optional so the Kafka
// consumer remains usable without a Mission Control client gateway.
func (c *KafkaConsumer) AttachWebSocketGateway(gateway *MissionControlWebSocketGateway) {
	if c == nil {
		return
	}
	c.webSocketMu.Lock()
	c.webSocket = gateway
	c.webSocketMu.Unlock()
}

func (c *KafkaConsumer) publishCommandExecution(record GroundRecord) {
	if c == nil || (record.Envelope.Kind != KindCommand && record.Envelope.Kind != KindEvent) {
		return
	}
	c.webSocketMu.RLock()
	gateway := c.webSocket
	c.webSocketMu.RUnlock()
	if gateway != nil {
		gateway.PublishCommandExecution(record)
	}
}

func (c *KafkaConsumer) publishAlert(alert MissionControlAlert) {
	if c == nil {
		return
	}
	c.webSocketMu.RLock()
	gateway := c.webSocket
	c.webSocketMu.RUnlock()
	if gateway != nil {
		gateway.PublishAlert(alert)
	}
}

func (c *KafkaConsumer) Alert(alertID, missionID string) (MissionControlAlert, bool) {
	if c == nil || c.alertManager == nil {
		return MissionControlAlert{}, false
	}
	return c.alertManager.Get(alertID, missionID)
}

func (c *KafkaConsumer) AlertManager() *MissionControlAlertManager {
	if c == nil {
		return nil
	}
	return c.alertManager
}

func (c *KafkaConsumer) Alerts(missionID string) MissionControlAlertSnapshot {
	if c == nil || c.alertManager == nil {
		return MissionControlAlertSnapshot{Version: currentMissionControlAlertVersion, Alerts: []MissionControlAlert{}}
	}
	return c.alertManager.Snapshot(missionID)
}

func (c *KafkaConsumer) publishTelemetry(record GroundRecord) {
	if c == nil || record.Envelope.Kind != KindTelemetry {
		return
	}
	c.webSocketMu.RLock()
	gateway := c.webSocket
	c.webSocketMu.RUnlock()
	if gateway != nil {
		gateway.PublishTelemetry(record)
	}
}

func (c *KafkaConsumer) AttachCommandOrchestrator(orchestrator *CommandExecutionOrchestrator) {
	if c == nil {
		return
	}
	c.orchestratorMu.Lock()
	c.orchestrator = orchestrator
	c.orchestratorMu.Unlock()
}

func (c *KafkaConsumer) setError(err error) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.snap.LastError = err.Error()
}

func (c *KafkaConsumer) Routes() RouteSnapshot {
	if c == nil || c.router == nil {
		return RouteSnapshot{}
	}
	return c.router.Snapshot()
}

func (c *KafkaConsumer) Results() map[DataKind]ProcessingResult {
	if c == nil || c.router == nil {
		return map[DataKind]ProcessingResult{}
	}
	return c.router.Results()
}

func (c *KafkaConsumer) CommandLifecycle() []CommandLifecycleEvaluation {
	if c == nil || c.router == nil {
		return []CommandLifecycleEvaluation{}
	}
	return c.router.CommandLifecycle()
}

func (c *KafkaConsumer) CommandExecution() []CommandExecutionReconciliation {
	if c == nil || c.router == nil {
		return []CommandExecutionReconciliation{}
	}
	return c.router.CommandExecution()
}

func (c *KafkaConsumer) CommandExecutionByID(commandID string) (CommandExecutionReconciliation, bool) {
	if c == nil || c.router == nil {
		return CommandExecutionReconciliation{}, false
	}
	return c.router.CommandExecutionByID(commandID)
}

func (c *KafkaConsumer) CommandExecutionForMission(missionID string) []CommandExecutionReconciliation {
	if c == nil || c.router == nil {
		return []CommandExecutionReconciliation{}
	}
	return c.router.CommandExecutionForMission(missionID)
}

func (c *KafkaConsumer) MissionState() map[string]MissionState {
	if c == nil || c.router == nil {
		return map[string]MissionState{}
	}
	return c.router.MissionState()
}

func (c *KafkaConsumer) VehicleSummaries(missionID string) []VehicleStateSummary {
	if c == nil || c.router == nil {
		return []VehicleStateSummary{}
	}
	return c.router.VehicleSummaries(missionID)
}

func (c *KafkaConsumer) Subsystems(missionID, sourceNode string) []SubsystemStateSummary {
	if c == nil || c.router == nil {
		return []SubsystemStateSummary{}
	}
	return c.router.Subsystems(missionID, sourceNode)
}

func (c *KafkaConsumer) Subsystem(missionID, sourceNode, subsystem string) (SubsystemStateSummary, bool) {
	if c == nil || c.router == nil {
		return SubsystemStateSummary{}, false
	}
	return c.router.Subsystem(missionID, sourceNode, subsystem)
}

func (c *KafkaConsumer) Vehicle(missionID, sourceNode string) (VehicleStateSummary, bool) {
	if c == nil || c.router == nil {
		return VehicleStateSummary{}, false
	}
	return c.router.Vehicle(missionID, sourceNode)
}

func (c *KafkaConsumer) MissionFreshness(missionID string, now time.Time, warnAfter, criticalAfter time.Duration) (MissionFreshnessSummary, bool, error) {
	if c == nil || c.router == nil {
		return MissionFreshnessSummary{}, false, nil
	}
	return c.router.MissionFreshness(missionID, now, warnAfter, criticalAfter)
}

func (c *KafkaConsumer) MissionHealth(missionID string) (MissionHealthSummary, bool) {
	if c == nil || c.router == nil {
		return MissionHealthSummary{}, false
	}
	return c.router.MissionHealth(missionID)
}

func (c *KafkaConsumer) MissionStateConsistency(ctx context.Context, repo RecordRepository, missionID string, checkedAt time.Time) (MissionStateConsistency, bool, error) {
	if c == nil || c.router == nil {
		return MissionStateConsistency{}, false, nil
	}
	return c.router.MissionStateConsistency(ctx, repo, missionID, checkedAt)
}

func (c *KafkaConsumer) SynchronizeMissionState(ctx context.Context, repo RecordRepository, missionID string, synchronizedAt time.Time) (MissionStateSynchronization, bool, error) {
	if c == nil || c.router == nil {
		return MissionStateSynchronization{}, false, nil
	}
	return c.router.SynchronizeMissionState(ctx, repo, missionID, synchronizedAt)
}

func (c *KafkaConsumer) MissionHealthState(missionID string, now time.Time, warnAfter, criticalAfter time.Duration) (MissionHealthState, bool, error) {
	if c == nil || c.router == nil {
		return MissionHealthState{}, false, nil
	}
	return c.router.MissionHealthState(missionID, now, warnAfter, criticalAfter)
}

func (c *KafkaConsumer) Mission(missionID string) (MissionState, bool) {
	if c == nil || c.router == nil {
		return MissionState{}, false
	}
	return c.router.Mission(missionID)
}

func (c *KafkaConsumer) Snapshot() ConsumerSnapshot {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.snap
}

func (c *KafkaConsumer) Close() error {
	if c == nil {
		return nil
	}
	c.cancel()
	c.wg.Wait()
	return c.reader.Close()
}
