package main

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"

	"github.com/segmentio/kafka-go"
)

// RecordPublisher publishes accepted ground records to the event stream.
// Implementations are deliberately behind an interface so local tests do not
// require a Kafka broker.
type RecordPublisher interface {
	Publish(ctx context.Context, record GroundRecord) error
	Close() error
}

// KafkaRecordPublisher is the first event-streaming boundary for the ground
// system. The source node is used as the message key so telemetry/events from the same
// vehicle/ground source are routed consistently to one Kafka partition.
type KafkaRecordPublisher struct {
	writer *kafka.Writer
}

func NewKafkaRecordPublisher(brokers string, topic string) (*KafkaRecordPublisher, error) {
	brokerList := splitCSV(brokers)
	if len(brokerList) == 0 {
		return nil, fmt.Errorf("kafka brokers are required")
	}
	topic = strings.TrimSpace(topic)
	if topic == "" {
		return nil, fmt.Errorf("kafka topic is required")
	}
	return &KafkaRecordPublisher{writer: &kafka.Writer{
		Addr:         kafka.TCP(brokerList...),
		Topic:        topic,
		Balancer:     &kafka.Hash{},
		RequiredAcks: kafka.RequireOne,
		Async:        false,
		BatchTimeout: 10 * 1000 * 1000, // 10ms
	}}, nil
}

func (p *KafkaRecordPublisher) Publish(ctx context.Context, record GroundRecord) error {
	if p == nil || p.writer == nil {
		return fmt.Errorf("kafka publisher is not initialized")
	}
	value, err := json.Marshal(record)
	if err != nil {
		return fmt.Errorf("encode kafka record: %w", err)
	}
	key := []byte(record.Envelope.SourceNode)
	if err := p.writer.WriteMessages(ctx, kafka.Message{Key: key, Value: value}); err != nil {
		return fmt.Errorf("publish ground record: %w", err)
	}
	return nil
}

func (p *KafkaRecordPublisher) Close() error {
	if p == nil || p.writer == nil {
		return nil
	}
	return p.writer.Close()
}

func splitCSV(value string) []string {
	parts := strings.Split(value, ",")
	out := make([]string, 0, len(parts))
	for _, part := range parts {
		if trimmed := strings.TrimSpace(part); trimmed != "" {
			out = append(out, trimmed)
		}
	}
	return out
}
