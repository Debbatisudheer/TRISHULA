package main

import (
	"bufio"
	"context"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const currentCommandOrchestratorVersion = "v0.9.87"

// VehicleExecutionResult is the domain-neutral result returned by a physical
// vehicle adapter. The default production adapter is the C++ bridge that wraps
// the existing VehicleCommandAdapter/SurfaceRover implementation.
type VehicleExecutionResult struct {
	Status            string
	Message           string
	Mode              string
	X                 float64
	Y                 float64
	Heading           float64
	Speed             float64
	BatterySOC        float64
	Deployed          bool
	MastReady         bool
	LocalizationValid bool
	HazardDetected    bool
	DriveHealthy      bool
	PhysicalSteps     uint64
}

type VehicleExecutor interface {
	Execute(ctx context.Context, record GroundRecord) (VehicleExecutionResult, error)
	Close() error
}

type cppVehicleCommandBridge struct {
	mu     sync.Mutex
	cmd    *exec.Cmd
	stdin  io.WriteCloser
	output *bufio.Reader
}

func NewCPPVehicleCommandBridge(path string) (*cppVehicleCommandBridge, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return nil, fmt.Errorf("TRISHULA_VEHICLE_BRIDGE_PATH is required")
	}
	if !filepath.IsAbs(path) {
		if abs, err := filepath.Abs(path); err == nil {
			path = abs
		}
	}
	if _, err := os.Stat(path); err != nil {
		return nil, fmt.Errorf("vehicle bridge %q is not available: %w", path, err)
	}
	cmd := exec.Command(path)

	// The Windows bridge is built with MSYS2 UCRT64. Go's child process does
	// not automatically inherit that runtime directory when the ground
	// service is launched from a normal PowerShell session. Add the runtime
	// directory explicitly so the bridge can start reliably. An explicit
	// TRISHULA_VEHICLE_BRIDGE_RUNTIME_PATH may be supplied for other installs.
	if runtimePath := strings.TrimSpace(os.Getenv("TRISHULA_VEHICLE_BRIDGE_RUNTIME_PATH")); runtimePath != "" {
		cmd.Env = append(os.Environ(), "PATH="+runtimePath+string(os.PathListSeparator)+os.Getenv("PATH"))
	} else if runtimePath := defaultVehicleBridgeRuntimePath(path); runtimePath != "" {
		cmd.Env = append(os.Environ(), "PATH="+runtimePath+string(os.PathListSeparator)+os.Getenv("PATH"))
	}

	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, fmt.Errorf("open vehicle bridge stdin: %w", err)
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		_ = stdin.Close()
		return nil, fmt.Errorf("open vehicle bridge stdout: %w", err)
	}
	if err := cmd.Start(); err != nil {
		_ = stdin.Close()
		return nil, fmt.Errorf("start vehicle bridge: %w", err)
	}
	return &cppVehicleCommandBridge{cmd: cmd, stdin: stdin, output: bufio.NewReader(stdout)}, nil
}

func defaultVehicleBridgeRuntimePath(bridgePath string) string {
	if runtime := strings.TrimSpace(os.Getenv("TRISHULA_VEHICLE_BRIDGE_RUNTIME_PATH")); runtime != "" {
		return runtime
	}
	if runtime := os.Getenv("MSYS2_UCRT64_BIN"); strings.TrimSpace(runtime) != "" {
		return runtime
	}
	if filepath.VolumeName(bridgePath) != "" {
		// Standard MSYS2 UCRT64 installation on Windows. Only add it when the
		// directory exists; this keeps Linux/non-MSYS2 deployments unchanged.
		if _, err := os.Stat(`C:\msys64\ucrt64\bin`); err == nil {
			return `C:\msys64\ucrt64\bin`
		}
	}
	return ""
}

func (b *cppVehicleCommandBridge) Execute(ctx context.Context, record GroundRecord) (VehicleExecutionResult, error) {
	if b == nil {
		return VehicleExecutionResult{}, fmt.Errorf("vehicle bridge is nil")
	}
	b.mu.Lock()
	defer b.mu.Unlock()

	paramsHex := hex.EncodeToString([]byte(strings.TrimSpace(record.Fields["parameters"])))
	// Dispatch commands encode optional parameters as the conventional x/y/dt
	// string under the same field used by the C++ adapter.
	if params := strings.TrimSpace(record.Fields["parameters"]); params != "" {
		paramsHex = hex.EncodeToString([]byte(params))
	}
	request := strings.Join([]string{
		record.Fields["command_id"],
		record.Fields["target"],
		record.Fields["command"],
		paramsHex,
		record.Envelope.MissionID,
		strconv.FormatUint(record.Envelope.SequenceNumber, 10),
		strconv.FormatUint(uint64(record.Envelope.ApplicationID), 10),
		strconv.FormatUint(record.Envelope.MissionTimestamp, 10),
	}, "\t") + "\n"

	if deadline, ok := ctx.Deadline(); ok {
		_ = deadline
	}
	if _, err := io.WriteString(b.stdin, request); err != nil {
		return VehicleExecutionResult{}, fmt.Errorf("write vehicle bridge request: %w", err)
	}

	lineCh := make(chan string, 1)
	errCh := make(chan error, 1)
	go func() {
		line, err := b.output.ReadString('\n')
		if err != nil {
			errCh <- err
			return
		}
		lineCh <- strings.TrimSpace(line)
	}()

	select {
	case <-ctx.Done():
		return VehicleExecutionResult{}, ctx.Err()
	case err := <-errCh:
		return VehicleExecutionResult{}, fmt.Errorf("read vehicle bridge response: %w", err)
	case line := <-lineCh:
		return parseVehicleBridgeResponse(line)
	}
}

func parseVehicleBridgeResponse(line string) (VehicleExecutionResult, error) {
	fields := strings.Split(line, "\t")
	if len(fields) == 2 && fields[0] == "ERROR" {
		message, err := hex.DecodeString(fields[1])
		if err != nil {
			return VehicleExecutionResult{}, fmt.Errorf("vehicle bridge error payload: %w", err)
		}
		return VehicleExecutionResult{}, fmt.Errorf("vehicle bridge: %s", string(message))
	}
	if len(fields) != 17 || fields[0] != "RESULT" {
		return VehicleExecutionResult{}, fmt.Errorf("malformed vehicle bridge response")
	}
	message, err := hex.DecodeString(fields[2])
	if err != nil {
		return VehicleExecutionResult{}, fmt.Errorf("decode bridge message: %w", err)
	}
	parseFloat := func(index int) (float64, error) { return strconv.ParseFloat(fields[index], 64) }
	x, err := parseFloat(4)
	if err != nil {
		return VehicleExecutionResult{}, err
	}
	y, err := parseFloat(5)
	if err != nil {
		return VehicleExecutionResult{}, err
	}
	heading, err := parseFloat(6)
	if err != nil {
		return VehicleExecutionResult{}, err
	}
	speed, err := parseFloat(7)
	if err != nil {
		return VehicleExecutionResult{}, err
	}
	battery, err := parseFloat(8)
	if err != nil {
		return VehicleExecutionResult{}, err
	}
	steps, err := strconv.ParseUint(fields[14], 10, 64)
	if err != nil {
		return VehicleExecutionResult{}, err
	}
	boolAt := func(index int) bool { return fields[index] == "1" }
	return VehicleExecutionResult{
		Status:            fields[1],
		Message:           string(message),
		Mode:              fields[3],
		X:                 x,
		Y:                 y,
		Heading:           heading,
		Speed:             speed,
		BatterySOC:        battery,
		Deployed:          boolAt(9),
		MastReady:         boolAt(10),
		LocalizationValid: boolAt(11),
		HazardDetected:    boolAt(12),
		DriveHealthy:      boolAt(13),
		PhysicalSteps:     steps,
	}, nil
}

func (b *cppVehicleCommandBridge) Close() error {
	if b == nil {
		return nil
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	_ = b.stdin.Close()
	if b.cmd.Process != nil {
		return b.cmd.Wait()
	}
	return nil
}

type CommandOrchestratorSnapshot struct {
	Enabled            bool      `json:"enabled"`
	Version            string    `json:"version"`
	QueueDepth         int       `json:"queue_depth"`
	WorkerCount        int       `json:"worker_count"`
	Submitted          uint64    `json:"submitted"`
	Executed           uint64    `json:"executed"`
	Failed             uint64    `json:"failed"`
	LastCommandID      string    `json:"last_command_id,omitempty"`
	LastCommandState   string    `json:"last_command_state,omitempty"`
	LastVehicleMessage string    `json:"last_vehicle_message,omitempty"`
	LastError          string    `json:"last_error,omitempty"`
	Retries            uint64    `json:"retries"`
	Timeouts           uint64    `json:"timeouts"`
	RetryAttempt       int       `json:"retry_attempt"`
	MaxAttempts        int       `json:"max_attempts"`
	AttemptTimeout     string    `json:"attempt_timeout"`
	InitialBackoff     string    `json:"initial_backoff"`
	MaxBackoff         string    `json:"max_backoff"`
	UpdatedAt          time.Time `json:"updated_at"`
}

type CommandExecutionOrchestrator struct {
	reliability CommandReliabilityPolicy
	queue       chan GroundRecord
	executor    VehicleExecutor
	emit        func(GroundRecord) IngestResult
	deadLetter  *CommandDeadLetterQueue
	cancel      context.CancelFunc
	wg          sync.WaitGroup
	mu          sync.RWMutex
	inFlight    map[string]bool
	terminal    map[string]string
	nextEvent   atomic.Uint64
	nextTelem   atomic.Uint64
	snap        CommandOrchestratorSnapshot
}

func NewCommandExecutionOrchestrator(ctx context.Context, executor VehicleExecutor, emit func(GroundRecord) IngestResult, workers, queueCapacity int) (*CommandExecutionOrchestrator, error) {
	return NewCommandExecutionOrchestratorWithPolicy(ctx, executor, emit, workers, queueCapacity, DefaultCommandReliabilityPolicy())
}

func NewCommandExecutionOrchestratorWithPolicy(ctx context.Context, executor VehicleExecutor, emit func(GroundRecord) IngestResult, workers, queueCapacity int, policy CommandReliabilityPolicy) (*CommandExecutionOrchestrator, error) {
	if executor == nil {
		return nil, fmt.Errorf("command orchestrator requires a vehicle executor")
	}
	if emit == nil {
		return nil, fmt.Errorf("command orchestrator requires an emit function")
	}
	if workers <= 0 {
		workers = 1
	}
	if queueCapacity <= 0 {
		queueCapacity = 64
	}
	child, cancel := context.WithCancel(ctx)
	normalizedPolicy := normalizeCommandReliabilityPolicy(policy)
	o := &CommandExecutionOrchestrator{
		reliability: normalizedPolicy,
		queue:       make(chan GroundRecord, queueCapacity),
		executor:    executor,
		emit:        emit,
		cancel:      cancel,
		inFlight:    make(map[string]bool),
		terminal:    make(map[string]string),
		snap:        CommandOrchestratorSnapshot{Enabled: true, Version: currentCommandOrchestratorVersion, WorkerCount: workers, MaxAttempts: normalizedPolicy.MaxAttempts, AttemptTimeout: normalizedPolicy.AttemptTimeout.String(), InitialBackoff: normalizedPolicy.InitialBackoff.String(), MaxBackoff: normalizedPolicy.MaxBackoff.String(), UpdatedAt: time.Now().UTC()},
	}
	o.nextEvent.Store(300000)
	o.nextTelem.Store(400000)
	for i := 0; i < workers; i++ {
		o.wg.Add(1)
		go o.worker(child)
	}
	return o, nil
}

func (o *CommandExecutionOrchestrator) SetDeadLetterQueue(q *CommandDeadLetterQueue) {
	if o == nil {
		return
	}
	o.mu.Lock()
	o.deadLetter = q
	o.mu.Unlock()
}

func (o *CommandExecutionOrchestrator) Replay(record GroundRecord) error {
	if o == nil {
		return fmt.Errorf("command orchestrator is nil")
	}
	commandID := resolveCommandID(record)
	if commandID == "" {
		return fmt.Errorf("command_id is required")
	}
	o.mu.Lock()
	delete(o.terminal, commandID)
	delete(o.inFlight, commandID)
	o.mu.Unlock()
	record.Fields["lifecycle"] = string(CommandReceived)
	return o.Submit(record)
}

func (o *CommandExecutionOrchestrator) Submit(record GroundRecord) error {
	if o == nil {
		return fmt.Errorf("command orchestrator is nil")
	}
	if record.Envelope.Kind != KindCommand {
		return fmt.Errorf("orchestrator received %q", record.Envelope.Kind)
	}
	lifecycle := strings.ToUpper(strings.TrimSpace(record.Fields["lifecycle"]))
	if lifecycle != string(CommandReceived) {
		return nil
	}
	commandID := resolveCommandID(record)
	if commandID == "" {
		return fmt.Errorf("command_id is required")
	}
	o.mu.Lock()
	if o.inFlight[commandID] || o.terminal[commandID] != "" {
		o.mu.Unlock()
		return nil
	}
	o.inFlight[commandID] = true
	o.snap.Submitted++
	o.snap.LastCommandID = commandID
	o.snap.LastCommandState = string(CommandReceived)
	o.snap.UpdatedAt = time.Now().UTC()
	o.mu.Unlock()

	select {
	case o.queue <- record:
		return nil
	default:
		o.mu.Lock()
		delete(o.inFlight, commandID)
		o.snap.LastError = "command execution queue is full"
		o.snap.UpdatedAt = time.Now().UTC()
		o.mu.Unlock()
		return fmt.Errorf("command execution queue is full")
	}
}

func (o *CommandExecutionOrchestrator) worker(ctx context.Context) {
	defer o.wg.Done()
	for {
		select {
		case <-ctx.Done():
			return
		case record := <-o.queue:
			_ = o.execute(ctx, record)
		}
	}
}

func (o *CommandExecutionOrchestrator) execute(ctx context.Context, record GroundRecord) error {
	commandID := resolveCommandID(record)
	if err := o.emitLifecycle(record, CommandQueued); err != nil {
		return o.finishFailure(commandID, err)
	}
	if err := o.emitLifecycle(record, CommandSent); err != nil {
		return o.finishFailure(commandID, err)
	}

	result, err, finalState := o.executeWithRetry(ctx, record)
	if err != nil {
		eventType := "COMMAND_FAILED"
		if finalState == CommandTimeoutExec {
			eventType = "COMMAND_TIMEOUT"
		}
		_ = o.emitEvent(record, eventType, finalState, err.Error(), nil)
		finishErr := o.finishFailureState(commandID, err, finalState)
		o.mu.RLock()
		deadLetter := o.deadLetter
		o.mu.RUnlock()
		if deadLetter != nil && (finalState == CommandFailedExec || finalState == CommandTimeoutExec) {
			if _, dlqErr := deadLetter.Enqueue(record, finalState, o.reliability.MaxAttempts, err); dlqErr != nil {
				o.mu.Lock()
				o.snap.LastError = fmt.Sprintf("%v; dead-letter: %v", err, dlqErr)
				o.snap.UpdatedAt = time.Now().UTC()
				o.mu.Unlock()
			}
		}
		return finishErr
	}
	if result.Status != "APPLIED" {
		err := fmt.Errorf("vehicle command %s: %s", result.Status, result.Message)
		_ = o.emitEvent(record, "COMMAND_FAILED", CommandFailedExec, result.Message, &result)
		finishErr := o.finishFailure(commandID, err)
		o.mu.RLock()
		deadLetter := o.deadLetter
		o.mu.RUnlock()
		if deadLetter != nil && isRetryableCommandFailure(result, nil) {
			_, _ = deadLetter.Enqueue(record, CommandFailedExec, o.reliability.MaxAttempts, err)
		}
		return finishErr
	}

	if err := o.emitEvent(record, "COMMAND_ACK", CommandAcknowledgedExec, result.Message, &result); err != nil {
		return o.finishFailure(commandID, err)
	}
	if err := o.emitEvent(record, "COMMAND_EXECUTING", CommandExecutingExec, result.Message, &result); err != nil {
		return o.finishFailure(commandID, err)
	}
	if err := o.emitEvent(record, "COMMAND_COMPLETED", CommandCompletedExec, result.Message, &result); err != nil {
		return o.finishFailure(commandID, err)
	}
	if err := o.emitTelemetry(record, &result); err != nil {
		return o.finishFailure(commandID, err)
	}

	o.mu.Lock()
	delete(o.inFlight, commandID)
	o.terminal[commandID] = string(CommandCompletedExec)
	o.snap.Executed++
	o.snap.LastCommandID = commandID
	o.snap.LastCommandState = string(CommandCompletedExec)
	o.snap.LastVehicleMessage = result.Message
	o.snap.LastError = ""
	o.snap.UpdatedAt = time.Now().UTC()
	o.mu.Unlock()
	return nil
}

func (o *CommandExecutionOrchestrator) executeWithRetry(parent context.Context, record GroundRecord) (VehicleExecutionResult, error, CommandExecutionState) {
	var lastErr error
	lastResult := VehicleExecutionResult{}
	finalState := CommandFailedExec
	for attempt := 1; attempt <= o.reliability.MaxAttempts; attempt++ {
		o.mu.Lock()
		o.snap.RetryAttempt = attempt
		o.snap.UpdatedAt = time.Now().UTC()
		o.mu.Unlock()
		attemptCtx, cancel := context.WithTimeout(parent, o.reliability.AttemptTimeout)
		result, err := o.executor.Execute(attemptCtx, record)
		cancel()
		lastResult = result
		if err == nil && result.Status == "APPLIED" {
			return result, nil, CommandCompletedExec
		}
		if err == nil {
			lastErr = fmt.Errorf("vehicle command %s: %s", result.Status, result.Message)
		} else {
			lastErr = err
		}
		finalState = CommandFailedExec
		if errors.Is(err, context.DeadlineExceeded) {
			finalState = CommandTimeoutExec
			o.mu.Lock()
			o.snap.Timeouts++
			o.snap.UpdatedAt = time.Now().UTC()
			o.mu.Unlock()
		}
		if attempt == o.reliability.MaxAttempts || !isRetryableCommandFailure(result, err) {
			return lastResult, lastErr, finalState
		}
		backoff := o.reliability.Backoff(attempt)
		_ = o.emitEvent(record, "COMMAND_RETRY", CommandSentExec, lastErr.Error(), nil)
		o.mu.Lock()
		o.snap.Retries++
		o.snap.RetryAttempt = attempt + 1
		o.snap.LastError = lastErr.Error()
		o.snap.UpdatedAt = time.Now().UTC()
		o.mu.Unlock()
		timer := time.NewTimer(backoff)
		select {
		case <-parent.Done():
			if !timer.Stop() {
				<-timer.C
			}
			return lastResult, parent.Err(), CommandCancelledExec
		case <-timer.C:
		}
	}
	return lastResult, lastErr, finalState
}

func isRetryableCommandFailure(result VehicleExecutionResult, err error) bool {
	if err != nil {
		return true
	}
	switch strings.ToUpper(strings.TrimSpace(result.Status)) {
	case "REJECTED", "DENIED", "INVALID", "UNSUPPORTED":
		return false
	default:
		return true
	}
}

func (o *CommandExecutionOrchestrator) finishFailure(commandID string, err error) error {
	return o.finishFailureState(commandID, err, CommandFailedExec)
}

func (o *CommandExecutionOrchestrator) finishFailureState(commandID string, err error, state CommandExecutionState) error {
	o.mu.Lock()
	defer o.mu.Unlock()
	delete(o.inFlight, commandID)
	o.terminal[commandID] = string(state)
	o.snap.Failed++
	o.snap.LastCommandID = commandID
	o.snap.LastCommandState = string(state)
	o.snap.LastError = err.Error()
	o.snap.UpdatedAt = time.Now().UTC()
	return err
}

func (o *CommandExecutionOrchestrator) emitLifecycle(source GroundRecord, state CommandLifecycle) error {
	seq := o.nextEvent.Add(1)
	id := fmt.Sprintf("%s-%s", resolveCommandID(source), strings.ToLower(string(state)))
	record := GroundRecord{
		Envelope: GroundEnvelope{SchemaVersion: 1, Kind: KindCommand, RecordID: id, MissionID: source.Envelope.MissionID, SourceNode: source.Envelope.SourceNode, OriginNode: source.Envelope.OriginNode, DestinationNode: source.Envelope.DestinationNode, Priority: source.Envelope.Priority, ApplicationID: source.Envelope.ApplicationID, MissionTimestamp: uint64(time.Now().UTC().UnixNano()), SequenceNumber: seq, Quality: 1.0, CorrelationID: resolveCommandID(source), PayloadSchema: "command.v1"},
		Fields:   map[string]string{"command_id": resolveCommandID(source), "command": source.Fields["command"], "target": source.Fields["target"], "lifecycle": string(state)},
	}
	if source.Fields["reason"] != "" {
		record.Fields["reason"] = source.Fields["reason"]
	}
	result := o.emit(record)
	if !result.Accepted {
		return fmt.Errorf("emit %s lifecycle failed: %s", state, result.Reason)
	}
	o.mu.Lock()
	o.snap.LastCommandState = string(state)
	o.snap.UpdatedAt = time.Now().UTC()
	o.mu.Unlock()
	return nil
}

func (o *CommandExecutionOrchestrator) emitEvent(source GroundRecord, eventType string, state CommandExecutionState, message string, vehicle *VehicleExecutionResult) error {
	seq := o.nextEvent.Add(1)
	fields := map[string]string{"command_id": resolveCommandID(source), "event_type": eventType, "severity": "INFO", "event_class": "COMMAND", "execution_state": string(state), "vehicle_response": message}
	if vehicle != nil {
		fields["mode"] = vehicle.Mode
		fields["speed_m_s"] = strconv.FormatFloat(vehicle.Speed, 'f', 6, 64)
		fields["battery_soc"] = strconv.FormatFloat(vehicle.BatterySOC, 'f', 6, 64)
	}
	record := GroundRecord{Envelope: GroundEnvelope{SchemaVersion: 1, Kind: KindEvent, RecordID: fmt.Sprintf("%s-%s", resolveCommandID(source), strings.ToLower(eventType)), MissionID: source.Envelope.MissionID, SourceNode: source.Envelope.DestinationNode, OriginNode: source.Envelope.DestinationNode, DestinationNode: source.Envelope.SourceNode, Priority: "high", ApplicationID: 301, MissionTimestamp: uint64(time.Now().UTC().UnixNano()), SequenceNumber: seq, Quality: 1.0, CorrelationID: resolveCommandID(source), PayloadSchema: "event.v1"}, Fields: fields}
	result := o.emit(record)
	if !result.Accepted {
		return fmt.Errorf("emit %s failed: %s", eventType, result.Reason)
	}
	o.mu.Lock()
	o.snap.LastCommandState = string(state)
	o.snap.LastVehicleMessage = message
	o.snap.UpdatedAt = time.Now().UTC()
	o.mu.Unlock()
	return nil
}

func (o *CommandExecutionOrchestrator) emitTelemetry(source GroundRecord, vehicle *VehicleExecutionResult) error {
	seq := o.nextTelem.Add(1)
	fields := map[string]string{"metric": "command_state", "mode": vehicle.Mode, "x_m": strconv.FormatFloat(vehicle.X, 'f', 6, 64), "y_m": strconv.FormatFloat(vehicle.Y, 'f', 6, 64), "heading_rad": strconv.FormatFloat(vehicle.Heading, 'f', 6, 64), "speed_m_s": strconv.FormatFloat(vehicle.Speed, 'f', 6, 64), "battery_soc": strconv.FormatFloat(vehicle.BatterySOC, 'f', 6, 64), "deployed": strconv.FormatBool(vehicle.Deployed), "mast_ready": strconv.FormatBool(vehicle.MastReady), "hazard_detected": strconv.FormatBool(vehicle.HazardDetected), "drive_system_healthy": strconv.FormatBool(vehicle.DriveHealthy), "physical_steps": strconv.FormatUint(vehicle.PhysicalSteps, 10)}
	record := GroundRecord{Envelope: GroundEnvelope{SchemaVersion: 1, Kind: KindTelemetry, RecordID: fmt.Sprintf("%s-telemetry-%d", resolveCommandID(source), seq), MissionID: source.Envelope.MissionID, SourceNode: source.Envelope.DestinationNode, OriginNode: source.Envelope.DestinationNode, DestinationNode: source.Envelope.SourceNode, Priority: "normal", ApplicationID: 103, MissionTimestamp: uint64(time.Now().UTC().UnixNano()), SequenceNumber: seq, Quality: 1.0, CorrelationID: resolveCommandID(source), PayloadSchema: "telemetry.v1"}, Fields: fields}
	result := o.emit(record)
	if !result.Accepted {
		return fmt.Errorf("emit command telemetry failed: %s", result.Reason)
	}
	return nil
}

func (o *CommandExecutionOrchestrator) Snapshot() CommandOrchestratorSnapshot {
	if o == nil {
		return CommandOrchestratorSnapshot{}
	}
	o.mu.RLock()
	defer o.mu.RUnlock()
	snapshot := o.snap
	snapshot.QueueDepth = len(o.queue)
	return snapshot
}

func (o *CommandExecutionOrchestrator) Close() error {
	if o == nil {
		return nil
	}
	o.cancel()
	o.wg.Wait()
	return o.executor.Close()
}
