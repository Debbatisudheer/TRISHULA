package main

import (
	"container/heap"
	"context"
	"fmt"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const currentCommandSchedulerVersion = "v0.9.108.1"
const commandSchedulePayloadSchema = "command.schedule.v1"

type CommandScheduleState string

const (
	CommandSchedulePending    CommandScheduleState = "SCHEDULED"
	CommandScheduleDispatched CommandScheduleState = "DISPATCHED"
	CommandScheduleCancelled  CommandScheduleState = "CANCELLED"
	CommandScheduleExpired    CommandScheduleState = "EXPIRED"
)

type CommandScheduleRequest struct {
	ScheduleID     string    `json:"schedule_id"`
	CommandID      string    `json:"command_id"`
	Command        string    `json:"command"`
	Target         string    `json:"target"`
	Priority       string    `json:"priority,omitempty"`
	Reason         string    `json:"reason,omitempty"`
	Parameters     string    `json:"parameters,omitempty"`
	SequenceNumber uint64    `json:"sequence_number"`
	SourceNode     string    `json:"source_node,omitempty"`
	ApplicationID  uint16    `json:"application_id,omitempty"`
	ScheduledAt    time.Time `json:"scheduled_at"`
}

type CommandSchedule struct {
	ScheduleID     string               `json:"schedule_id"`
	CommandID      string               `json:"command_id"`
	MissionID      string               `json:"mission_id"`
	Command        string               `json:"command"`
	Target         string               `json:"target"`
	Priority       string               `json:"priority"`
	Reason         string               `json:"reason,omitempty"`
	Parameters     string               `json:"parameters,omitempty"`
	SequenceNumber uint64               `json:"sequence_number"`
	SourceNode     string               `json:"source_node"`
	ApplicationID  uint16               `json:"application_id"`
	ScheduledAt    time.Time            `json:"scheduled_at"`
	State          CommandScheduleState `json:"state"`
	CreatedAt      time.Time            `json:"created_at"`
	DispatchedAt   time.Time            `json:"dispatched_at,omitempty"`
	CancelledAt    time.Time            `json:"cancelled_at,omitempty"`
	UpdatedAt      time.Time            `json:"updated_at"`
	Error          string               `json:"error,omitempty"`
}

type CommandSchedulerSnapshot struct {
	Enabled         bool      `json:"enabled"`
	Version         string    `json:"version"`
	Pending         int       `json:"pending"`
	Dispatched      uint64    `json:"dispatched"`
	Cancelled       uint64    `json:"cancelled"`
	Expired         uint64    `json:"expired"`
	SchedulerErrors uint64    `json:"scheduler_errors"`
	NextScheduledAt time.Time `json:"next_scheduled_at,omitempty"`
	UpdatedAt       time.Time `json:"updated_at"`
}

type commandScheduleHeap []CommandSchedule

func (h commandScheduleHeap) Len() int { return len(h) }
func (h commandScheduleHeap) Less(i, j int) bool {
	if h[i].ScheduledAt.Equal(h[j].ScheduledAt) {
		return h[i].ScheduleID < h[j].ScheduleID
	}
	return h[i].ScheduledAt.Before(h[j].ScheduledAt)
}
func (h commandScheduleHeap) Swap(i, j int) { h[i], h[j] = h[j], h[i] }
func (h *commandScheduleHeap) Push(x any)   { *h = append(*h, x.(CommandSchedule)) }
func (h *commandScheduleHeap) Pop() any {
	old := *h
	n := len(old)
	x := old[n-1]
	*h = old[:n-1]
	return x
}

// CommandScheduler maintains a min-heap of pending schedules so the dispatch
// loop can inspect the earliest due command without sorting every schedule on
// every polling tick. State changes are reflected in the map; stale heap
// entries are discarded lazily when they reach the top.
type CommandScheduler struct {
	mu         sync.RWMutex
	schedules  map[string]CommandSchedule
	pending    commandScheduleHeap
	dispatcher *MissionCommandDispatcher
	service    *Service
	cancel     context.CancelFunc
	wg         sync.WaitGroup
	seq        atomic.Uint64
	snap       CommandSchedulerSnapshot
}

func NewCommandScheduler(ctx context.Context, service *Service, dispatcher *MissionCommandDispatcher, poll time.Duration) (*CommandScheduler, error) {
	if service == nil || dispatcher == nil {
		return nil, fmt.Errorf("command scheduler requires service and dispatcher")
	}
	if poll <= 0 {
		poll = time.Second
	}
	s := &CommandScheduler{service: service, dispatcher: dispatcher, schedules: make(map[string]CommandSchedule), pending: make(commandScheduleHeap, 0), snap: CommandSchedulerSnapshot{Enabled: true, Version: currentCommandSchedulerVersion, UpdatedAt: time.Now().UTC()}}
	records, err := service.LoadRecords()
	if err != nil {
		return nil, err
	}
	s.rebuild(records)
	child, cancel := context.WithCancel(ctx)
	s.cancel = cancel
	s.wg.Add(1)
	go s.loop(child, poll)
	return s, nil
}

func (s *CommandScheduler) rebuild(records []GroundRecord) {
	s.mu.Lock()
	defer s.mu.Unlock()
	for _, r := range records {
		if r.Envelope.Kind != KindEvent || r.Fields["event_type"] != "COMMAND_SCHEDULE" {
			continue
		}
		schedule, err := scheduleFromRecord(r)
		if err != nil {
			continue
		}
		if previous, ok := s.schedules[schedule.ScheduleID]; !ok || schedule.UpdatedAt.After(previous.UpdatedAt) {
			s.schedules[schedule.ScheduleID] = schedule
		}
	}
	s.rebuildPendingHeapLocked()
	s.refreshLocked(time.Now().UTC())
}

func (s *CommandScheduler) Create(missionID string, req CommandScheduleRequest) (CommandSchedule, error) {
	missionID = strings.TrimSpace(missionID)
	if missionID == "" {
		return CommandSchedule{}, fmt.Errorf("missionID is required")
	}
	if strings.TrimSpace(req.ScheduleID) == "" {
		return CommandSchedule{}, fmt.Errorf("schedule_id is required")
	}
	if strings.TrimSpace(req.CommandID) == "" {
		return CommandSchedule{}, fmt.Errorf("command_id is required")
	}
	if strings.TrimSpace(req.Command) == "" || strings.TrimSpace(req.Target) == "" {
		return CommandSchedule{}, fmt.Errorf("command and target are required")
	}
	if req.SequenceNumber == 0 {
		return CommandSchedule{}, fmt.Errorf("sequence_number must be positive")
	}
	if req.ScheduledAt.IsZero() {
		return CommandSchedule{}, fmt.Errorf("scheduled_at is required")
	}
	now := time.Now().UTC()
	at := req.ScheduledAt.UTC()
	if at.Before(now.Add(-5 * time.Second)) {
		return CommandSchedule{}, fmt.Errorf("scheduled_at must not be in the past")
	}
	priority := strings.TrimSpace(req.Priority)
	if priority == "" {
		priority = "normal"
	}
	source := strings.TrimSpace(req.SourceNode)
	if source == "" {
		source = s.dispatcher.sourceNode
	}
	app := req.ApplicationID
	if app == 0 {
		app = 401
	}
	schedule := CommandSchedule{ScheduleID: strings.TrimSpace(req.ScheduleID), CommandID: strings.TrimSpace(req.CommandID), MissionID: missionID, Command: strings.ToUpper(strings.TrimSpace(req.Command)), Target: strings.TrimSpace(req.Target), Priority: priority, Reason: strings.TrimSpace(req.Reason), Parameters: strings.TrimSpace(req.Parameters), SequenceNumber: req.SequenceNumber, SourceNode: source, ApplicationID: app, ScheduledAt: at, State: CommandSchedulePending, CreatedAt: now, UpdatedAt: now}
	s.mu.Lock()
	if _, ok := s.schedules[schedule.ScheduleID]; ok {
		s.mu.Unlock()
		return CommandSchedule{}, fmt.Errorf("schedule_id %q already exists", schedule.ScheduleID)
	}
	for _, existing := range s.schedules {
		if existing.CommandID == schedule.CommandID && existing.State == CommandSchedulePending {
			s.mu.Unlock()
			return CommandSchedule{}, fmt.Errorf("command_id %q is already scheduled", schedule.CommandID)
		}
	}
	s.schedules[schedule.ScheduleID] = schedule
	heap.Push(&s.pending, schedule)
	s.refreshLocked(now)
	s.mu.Unlock()
	if err := s.persist(schedule); err != nil {
		s.mu.Lock()
		delete(s.schedules, schedule.ScheduleID)
		s.removeStaleHeapTopLocked()
		s.refreshLocked(time.Now().UTC())
		s.mu.Unlock()
		return CommandSchedule{}, err
	}
	return schedule, nil
}

func (s *CommandScheduler) Cancel(scheduleID string) (CommandSchedule, error) {
	id := strings.TrimSpace(scheduleID)
	if id == "" {
		return CommandSchedule{}, fmt.Errorf("scheduleID is required")
	}
	s.mu.Lock()
	schedule, ok := s.schedules[id]
	if !ok {
		s.mu.Unlock()
		return CommandSchedule{}, fmt.Errorf("schedule not found")
	}
	if schedule.State != CommandSchedulePending {
		s.mu.Unlock()
		return schedule, fmt.Errorf("schedule is already %s", schedule.State)
	}
	schedule.State = CommandScheduleCancelled
	schedule.CancelledAt = time.Now().UTC()
	schedule.UpdatedAt = schedule.CancelledAt
	s.schedules[id] = schedule
	s.snap.Cancelled++
	s.refreshLocked(schedule.UpdatedAt)
	s.mu.Unlock()
	if err := s.persist(schedule); err != nil {
		return schedule, err
	}
	return schedule, nil
}

func (s *CommandScheduler) List(missionID string) []CommandSchedule {
	s.mu.RLock()
	defer s.mu.RUnlock()
	out := make([]CommandSchedule, 0, len(s.schedules))
	for _, v := range s.schedules {
		if missionID == "" || v.MissionID == missionID {
			out = append(out, v)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ScheduledAt.Before(out[j].ScheduledAt) })
	return out
}
func (s *CommandScheduler) Get(id string) (CommandSchedule, bool) {
	s.mu.RLock()
	defer s.mu.RUnlock()
	v, ok := s.schedules[strings.TrimSpace(id)]
	return v, ok
}
func (s *CommandScheduler) Snapshot() CommandSchedulerSnapshot {
	s.mu.RLock()
	defer s.mu.RUnlock()
	return s.snap
}
func (s *CommandScheduler) Close() error {
	if s == nil {
		return nil
	}
	if s.cancel != nil {
		s.cancel()
	}
	s.wg.Wait()
	return nil
}

func (s *CommandScheduler) loop(ctx context.Context, poll time.Duration) {
	defer s.wg.Done()
	t := time.NewTicker(poll)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case now := <-t.C:
			s.dispatchDue(ctx, now.UTC())
		}
	}
}
func (s *CommandScheduler) dispatchDue(ctx context.Context, now time.Time) {
	for {
		s.mu.Lock()
		s.removeStaleHeapTopLocked()
		if len(s.pending) == 0 || s.pending[0].ScheduledAt.After(now) {
			s.refreshLocked(now)
			s.mu.Unlock()
			return
		}
		sch := heap.Pop(&s.pending).(CommandSchedule)
		current, ok := s.schedules[sch.ScheduleID]
		if !ok || current.State != CommandSchedulePending || current.ScheduledAt.After(now) {
			s.refreshLocked(now)
			s.mu.Unlock()
			continue
		}
		s.mu.Unlock()

		req := CommandDispatchRequest{CommandID: sch.CommandID, Command: sch.Command, Target: sch.Target, Priority: sch.Priority, Reason: sch.Reason, Parameters: sch.Parameters, SequenceNumber: sch.SequenceNumber, SourceNode: sch.SourceNode, ApplicationID: sch.ApplicationID}
		if _, err := s.dispatcher.Submit(sch.MissionID, req); err != nil {
			s.mu.Lock()
			v := s.schedules[sch.ScheduleID]
			v.State = CommandScheduleExpired
			v.Error = err.Error()
			v.UpdatedAt = now
			s.schedules[sch.ScheduleID] = v
			s.snap.Expired++
			s.snap.SchedulerErrors++
			s.refreshLocked(now)
			s.mu.Unlock()
			_ = s.persist(v)
			continue
		}
		s.mu.Lock()
		v := s.schedules[sch.ScheduleID]
		v.State = CommandScheduleDispatched
		v.DispatchedAt = now
		v.UpdatedAt = now
		s.schedules[sch.ScheduleID] = v
		s.snap.Dispatched++
		s.refreshLocked(now)
		s.mu.Unlock()
		_ = s.persist(v)
		_ = ctx
	}
}

func (s *CommandScheduler) persist(schedule CommandSchedule) error {
	s.seq.Add(1)
	record := scheduleRecord(schedule, s.seq.Load())
	result := s.service.Ingest(record)
	if !result.Accepted && !result.Duplicate {
		return fmt.Errorf("persist schedule: %s", result.Reason)
	}
	return nil
}
func (s *CommandScheduler) refreshLocked(now time.Time) {
	pending := 0
	for _, v := range s.schedules {
		if v.State == CommandSchedulePending {
			pending++
		}
	}
	s.removeStaleHeapTopLocked()
	var next time.Time
	if len(s.pending) > 0 {
		next = s.pending[0].ScheduledAt
	}
	s.snap.Pending = pending
	s.snap.NextScheduledAt = next
	s.snap.UpdatedAt = now
}

func (s *CommandScheduler) rebuildPendingHeapLocked() {
	s.pending = s.pending[:0]
	for _, v := range s.schedules {
		if v.State == CommandSchedulePending {
			s.pending = append(s.pending, v)
		}
	}
	heap.Init(&s.pending)
}

func (s *CommandScheduler) removeStaleHeapTopLocked() {
	for len(s.pending) > 0 {
		top := s.pending[0]
		current, ok := s.schedules[top.ScheduleID]
		if ok && current.State == CommandSchedulePending && current.ScheduledAt.Equal(top.ScheduledAt) {
			return
		}
		heap.Pop(&s.pending)
	}
}

func scheduleRecord(s CommandSchedule, seq uint64) GroundRecord {
	fields := map[string]string{"event_type": "COMMAND_SCHEDULE", "schedule_id": s.ScheduleID, "command_id": s.CommandID, "command": s.Command, "target": s.Target, "priority": s.Priority, "source_node": s.SourceNode, "application_id": fmt.Sprintf("%d", s.ApplicationID), "sequence_number": fmt.Sprintf("%d", s.SequenceNumber), "scheduled_at": s.ScheduledAt.Format(time.RFC3339Nano), "state": string(s.State), "created_at": s.CreatedAt.Format(time.RFC3339Nano), "updated_at": s.UpdatedAt.Format(time.RFC3339Nano)}
	if s.Reason != "" {
		fields["reason"] = s.Reason
	}
	if s.Parameters != "" {
		fields["parameters"] = s.Parameters
	}
	if !s.DispatchedAt.IsZero() {
		fields["dispatched_at"] = s.DispatchedAt.Format(time.RFC3339Nano)
	}
	if !s.CancelledAt.IsZero() {
		fields["cancelled_at"] = s.CancelledAt.Format(time.RFC3339Nano)
	}
	if s.Error != "" {
		fields["error"] = s.Error
	}
	return GroundRecord{Envelope: GroundEnvelope{SchemaVersion: 1, Kind: KindEvent, RecordID: fmt.Sprintf("schedule-%s-%d", s.ScheduleID, seq), MissionID: s.MissionID, SourceNode: "GROUND-SCHEDULER", OriginNode: "GROUND-SCHEDULER", DestinationNode: "GROUND-OPS", Priority: s.Priority, ApplicationID: 402, MissionTimestamp: uint64(time.Now().UTC().UnixNano()), SequenceNumber: seq, Quality: 1, CorrelationID: s.CommandID, PayloadSchema: commandSchedulePayloadSchema}, Fields: fields}
}
func scheduleFromRecord(r GroundRecord) (CommandSchedule, error) {
	f := r.Fields
	at, err := time.Parse(time.RFC3339Nano, f["scheduled_at"])
	if err != nil {
		return CommandSchedule{}, err
	}
	created, _ := time.Parse(time.RFC3339Nano, f["created_at"])
	updated, _ := time.Parse(time.RFC3339Nano, f["updated_at"])
	seq, _ := parseUint(f["sequence_number"])
	app, _ := parseUint(f["application_id"])
	s := CommandSchedule{ScheduleID: f["schedule_id"], CommandID: f["command_id"], MissionID: r.Envelope.MissionID, Command: f["command"], Target: f["target"], Priority: f["priority"], SourceNode: f["source_node"], ApplicationID: uint16(app), SequenceNumber: seq, ScheduledAt: at, State: CommandScheduleState(f["state"]), CreatedAt: created, UpdatedAt: updated, Reason: f["reason"], Parameters: f["parameters"], Error: f["error"]}
	if v, e := time.Parse(time.RFC3339Nano, f["dispatched_at"]); e == nil {
		s.DispatchedAt = v
	}
	if v, e := time.Parse(time.RFC3339Nano, f["cancelled_at"]); e == nil {
		s.CancelledAt = v
	}
	return s, nil
}
func parseUint(v string) (uint64, error) {
	var n uint64
	_, err := fmt.Sscan(strings.TrimSpace(v), &n)
	return n, err
}
