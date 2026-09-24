package main

import (
	"context"
	"encoding/json"
	"fmt"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const currentCommandDeadLetterVersion = "v0.9.87"

type CommandDeadLetterState string

const (
	CommandDLQPending  CommandDeadLetterState = "PENDING"
	CommandDLQReplayed CommandDeadLetterState = "REPLAYED"
	CommandDLQResolved CommandDeadLetterState = "RESOLVED"
)

type CommandDeadLetterEntry struct {
	DeadLetterID   string                 `json:"dead_letter_id"`
	CommandID      string                 `json:"command_id"`
	MissionID      string                 `json:"mission_id"`
	Target         string                 `json:"target"`
	Command        string                 `json:"command"`
	State          CommandDeadLetterState `json:"state"`
	ExecutionState CommandExecutionState  `json:"execution_state"`
	Attempts       int                    `json:"attempts"`
	Error          string                 `json:"error,omitempty"`
	CreatedAt      time.Time              `json:"created_at"`
	UpdatedAt      time.Time              `json:"updated_at"`
	ReplayedAt     time.Time              `json:"replayed_at,omitempty"`
	ResolvedAt     time.Time              `json:"resolved_at,omitempty"`
	Record         GroundRecord           `json:"record"`
}

type CommandDeadLetterSnapshot struct {
	Enabled        bool      `json:"enabled"`
	Version        string    `json:"version"`
	Pending        int       `json:"pending"`
	Replayed       uint64    `json:"replayed"`
	Resolved       uint64    `json:"resolved"`
	Total          int       `json:"total"`
	LastCommandID  string    `json:"last_command_id,omitempty"`
	LastDeadLetter string    `json:"last_dead_letter_id,omitempty"`
	UpdatedAt      time.Time `json:"updated_at"`
}

type CommandDeadLetterQueue struct {
	mu      sync.RWMutex
	entries map[string]CommandDeadLetterEntry
	emit    func(GroundRecord) IngestResult
	replay  func(GroundRecord) error
	nextSeq atomic.Uint64
	snap    CommandDeadLetterSnapshot
}

func NewCommandDeadLetterQueue(emit func(GroundRecord) IngestResult, load func() ([]GroundRecord, error)) *CommandDeadLetterQueue {
	q := &CommandDeadLetterQueue{
		entries: make(map[string]CommandDeadLetterEntry),
		emit:    emit,
		snap: CommandDeadLetterSnapshot{
			Enabled:   true,
			Version:   currentCommandDeadLetterVersion,
			UpdatedAt: time.Now().UTC(),
		},
	}
	q.nextSeq.Store(500000)
	if load != nil {
		if records, err := load(); err == nil {
			q.rebuild(records)
		}
	}
	return q
}

func (q *CommandDeadLetterQueue) SetReplay(fn func(GroundRecord) error) {
	q.mu.Lock()
	defer q.mu.Unlock()
	q.replay = fn
}

func (q *CommandDeadLetterQueue) rebuild(records []GroundRecord) {
	type action struct {
		id        string
		commandID string
		state     CommandDeadLetterState
		record    GroundRecord
		attempts  int
		err       string
		created   time.Time
		updated   time.Time
		replayed  time.Time
		resolved  time.Time
	}
	latest := make(map[string]action)
	for _, record := range records {
		if record.Envelope.Kind != KindEvent || strings.TrimSpace(record.Fields["event_type"]) != "COMMAND_DLQ" {
			continue
		}
		id := strings.TrimSpace(record.Fields["dead_letter_id"])
		if id == "" {
			continue
		}
		var original GroundRecord
		if raw := record.Fields["original_record"]; raw != "" {
			_ = json.Unmarshal([]byte(raw), &original)
		}
		attempts := 0
		if raw := record.Fields["attempts"]; raw != "" {
			_, _ = fmt.Sscanf(raw, "%d", &attempts)
		}
		created, _ := time.Parse(time.RFC3339Nano, record.Fields["created_at"])
		updated, _ := time.Parse(time.RFC3339Nano, record.Fields["updated_at"])
		replayed, _ := time.Parse(time.RFC3339Nano, record.Fields["replayed_at"])
		resolved, _ := time.Parse(time.RFC3339Nano, record.Fields["resolved_at"])
		a := action{id: id, commandID: record.Fields["command_id"], state: CommandDeadLetterState(strings.ToUpper(record.Fields["dlq_state"])), record: original, attempts: attempts, err: record.Fields["error"], created: created, updated: updated, replayed: replayed, resolved: resolved}
		if a.state == "" {
			a.state = CommandDLQPending
		}
		if previous, ok := latest[id]; !ok || record.Envelope.MissionTimestamp >= uint64(previous.updated.UnixNano()) {
			latest[id] = a
		}
	}
	q.mu.Lock()
	defer q.mu.Unlock()
	for id, a := range latest {
		if a.created.IsZero() {
			a.created = time.Now().UTC()
		}
		if a.updated.IsZero() {
			a.updated = a.created
		}
		q.entries[id] = CommandDeadLetterEntry{
			DeadLetterID: id, CommandID: a.commandID, MissionID: a.record.Envelope.MissionID,
			Target: a.record.Fields["target"], Command: a.record.Fields["command"], State: a.state,
			ExecutionState: CommandExecutionState(recordField(a.record, "execution_state")), Attempts: a.attempts,
			Error: a.err, CreatedAt: a.created, UpdatedAt: a.updated, ReplayedAt: a.replayed, ResolvedAt: a.resolved, Record: a.record,
		}
	}
	q.refreshLocked(time.Now().UTC())
}

func recordField(record GroundRecord, key string) string {
	return strings.TrimSpace(record.Fields[key])
}

func (q *CommandDeadLetterQueue) Enqueue(record GroundRecord, state CommandExecutionState, attempts int, err error) (CommandDeadLetterEntry, error) {
	if q == nil {
		return CommandDeadLetterEntry{}, fmt.Errorf("dead-letter queue is nil")
	}
	commandID := resolveCommandID(record)
	if commandID == "" {
		return CommandDeadLetterEntry{}, fmt.Errorf("command_id is required for dead-letter entry")
	}
	now := time.Now().UTC()
	id := fmt.Sprintf("DLQ-%s-%d", commandID, q.nextSeq.Add(1))
	message := ""
	if err != nil {
		message = err.Error()
	}
	entry := CommandDeadLetterEntry{DeadLetterID: id, CommandID: commandID, MissionID: record.Envelope.MissionID, Target: record.Fields["target"], Command: record.Fields["command"], State: CommandDLQPending, ExecutionState: state, Attempts: attempts, Error: message, CreatedAt: now, UpdatedAt: now, Record: record}
	q.mu.Lock()
	q.entries[id] = entry
	q.snap.LastCommandID = commandID
	q.snap.LastDeadLetter = id
	q.refreshLocked(now)
	q.mu.Unlock()
	if err := q.persistEvent(entry, "ENQUEUED"); err != nil {
		return entry, err
	}
	return entry, nil
}

func (q *CommandDeadLetterQueue) persistEvent(entry CommandDeadLetterEntry, action string) error {
	if q.emit == nil {
		return nil
	}
	raw, err := json.Marshal(entry.Record)
	if err != nil {
		return err
	}
	seq := q.nextSeq.Add(1)
	fields := map[string]string{
		"command_id":      entry.CommandID,
		"dead_letter_id":  entry.DeadLetterID,
		"event_type":      "COMMAND_DLQ",
		"event_class":     "COMMAND",
		"severity":        "ERROR",
		"dlq_action":      action,
		"dlq_state":       string(entry.State),
		"execution_state": string(entry.ExecutionState),
		"attempts":        fmt.Sprintf("%d", entry.Attempts),
		"error":           entry.Error,
		"created_at":      entry.CreatedAt.Format(time.RFC3339Nano),
		"updated_at":      entry.UpdatedAt.Format(time.RFC3339Nano),
		"original_record": string(raw),
	}
	if !entry.ReplayedAt.IsZero() {
		fields["replayed_at"] = entry.ReplayedAt.Format(time.RFC3339Nano)
	}
	if !entry.ResolvedAt.IsZero() {
		fields["resolved_at"] = entry.ResolvedAt.Format(time.RFC3339Nano)
	}
	result := q.emit(GroundRecord{Envelope: GroundEnvelope{SchemaVersion: 1, Kind: KindEvent, RecordID: fmt.Sprintf("%s-%s-%d", entry.DeadLetterID, strings.ToLower(action), seq), MissionID: entry.MissionID, SourceNode: "GROUND-OPS-01", OriginNode: "GROUND-OPS-01", DestinationNode: entry.Target, Priority: "high", ApplicationID: 302, MissionTimestamp: uint64(nowNano()), SequenceNumber: seq, Quality: 1.0, CorrelationID: entry.CommandID, PayloadSchema: "event.v1"}, Fields: fields})
	if !result.Accepted {
		return fmt.Errorf("persist dead-letter event failed: %s", result.Reason)
	}
	return nil
}

func nowNano() int64 { return time.Now().UTC().UnixNano() }

func (q *CommandDeadLetterQueue) Get(id string) (CommandDeadLetterEntry, bool) {
	q.mu.RLock()
	defer q.mu.RUnlock()
	entry, ok := q.entries[strings.TrimSpace(id)]
	return entry, ok
}

func (q *CommandDeadLetterQueue) List(missionID string) []CommandDeadLetterEntry {
	missionID = strings.TrimSpace(missionID)
	q.mu.RLock()
	values := make([]CommandDeadLetterEntry, 0, len(q.entries))
	for _, entry := range q.entries {
		if missionID == "" || entry.MissionID == missionID {
			values = append(values, entry)
		}
	}
	q.mu.RUnlock()
	sort.Slice(values, func(i, j int) bool { return values[i].CreatedAt.After(values[j].CreatedAt) })
	return values
}

func (q *CommandDeadLetterQueue) Replay(ctx context.Context, id string) (CommandDeadLetterEntry, error) {
	if err := ctx.Err(); err != nil {
		return CommandDeadLetterEntry{}, err
	}
	q.mu.Lock()
	entry, ok := q.entries[strings.TrimSpace(id)]
	if !ok {
		q.mu.Unlock()
		return CommandDeadLetterEntry{}, fmt.Errorf("dead-letter entry not found")
	}
	if entry.State != CommandDLQPending {
		q.mu.Unlock()
		return entry, fmt.Errorf("dead-letter entry %q is already %s", id, entry.State)
	}
	replay := q.replay
	if replay == nil {
		q.mu.Unlock()
		return entry, fmt.Errorf("dead-letter replay is not configured")
	}
	q.mu.Unlock()
	if err := replay(entry.Record); err != nil {
		return entry, err
	}
	now := time.Now().UTC()
	q.mu.Lock()
	entry.State = CommandDLQReplayed
	entry.ReplayedAt = now
	entry.UpdatedAt = now
	q.entries[entry.DeadLetterID] = entry
	q.snap.Replayed++
	q.snap.LastCommandID = entry.CommandID
	q.snap.LastDeadLetter = entry.DeadLetterID
	q.refreshLocked(now)
	q.mu.Unlock()
	if err := q.persistEvent(entry, "REPLAYED"); err != nil {
		return entry, err
	}
	return entry, nil
}

func (q *CommandDeadLetterQueue) Resolve(ctx context.Context, id string) (CommandDeadLetterEntry, error) {
	if err := ctx.Err(); err != nil {
		return CommandDeadLetterEntry{}, err
	}
	q.mu.Lock()
	entry, ok := q.entries[strings.TrimSpace(id)]
	if !ok {
		q.mu.Unlock()
		return CommandDeadLetterEntry{}, fmt.Errorf("dead-letter entry not found")
	}
	if entry.State == CommandDLQResolved {
		q.mu.Unlock()
		return entry, nil
	}
	now := time.Now().UTC()
	entry.State = CommandDLQResolved
	entry.ResolvedAt = now
	entry.UpdatedAt = now
	q.entries[entry.DeadLetterID] = entry
	q.snap.Resolved++
	q.snap.LastCommandID = entry.CommandID
	q.snap.LastDeadLetter = entry.DeadLetterID
	q.refreshLocked(now)
	q.mu.Unlock()
	if err := q.persistEvent(entry, "RESOLVED"); err != nil {
		return entry, err
	}
	return entry, nil
}

func (q *CommandDeadLetterQueue) Snapshot() CommandDeadLetterSnapshot {
	q.mu.RLock()
	defer q.mu.RUnlock()
	snap := q.snap
	snap.Pending = 0
	for _, entry := range q.entries {
		if entry.State == CommandDLQPending {
			snap.Pending++
		}
	}
	snap.Total = len(q.entries)
	return snap
}

func (q *CommandDeadLetterQueue) refreshLocked(now time.Time) {
	q.snap.Total = len(q.entries)
	q.snap.UpdatedAt = now
}
