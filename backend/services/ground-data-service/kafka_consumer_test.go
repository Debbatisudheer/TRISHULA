package main

import "testing"

func TestKafkaConsumerConfigurationRejectsMissingValues(t *testing.T) {
	if _, err := NewKafkaRecordConsumer("", "topic", "group"); err == nil {
		t.Fatal("expected missing broker error")
	}
	if _, err := NewKafkaRecordConsumer("localhost:9092", "", "group"); err == nil {
		t.Fatal("expected missing topic error")
	}
	if _, err := NewKafkaRecordConsumer("localhost:9092", "topic", ""); err == nil {
		t.Fatal("expected missing group id error")
	}
}

func TestConsumerSnapshotDefaults(t *testing.T) {
	snapshot := ConsumerSnapshot{Enabled: true, GroupID: "g", Topic: "t", Broker: "b"}
	if !snapshot.Enabled || snapshot.GroupID != "g" || snapshot.Topic != "t" {
		t.Fatalf("unexpected snapshot: %+v", snapshot)
	}
}
