package main

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

const currentPhysicalMissionRuntimeVersion = "v0.9.113.1"

type PhysicalMissionRuntimeSnapshot struct {
	Result              string  `json:"result"`
	State               string  `json:"state"`
	ControlSequence     uint64  `json:"control_sequence"`
	TelemetrySequence   uint64  `json:"telemetry_sequence"`
	MissionTimeSeconds  float64 `json:"mission_time_seconds"`
	PhaseElapsedSeconds float64 `json:"phase_elapsed_seconds"`
	PhaseProgress       float64 `json:"phase_progress"`
	PositionX           float64 `json:"position_x_m"`
	PositionY           float64 `json:"position_y_m"`
	PositionZ           float64 `json:"position_z_m"`
	VelocityX           float64 `json:"velocity_x_m_per_s"`
	VelocityY           float64 `json:"velocity_y_m_per_s"`
	VelocityZ           float64 `json:"velocity_z_m_per_s"`
	Altitude            float64 `json:"altitude_m"`
	Speed               float64 `json:"speed_m_per_s"`
	DistanceToMoon      float64 `json:"distance_to_moon_m"`
	MoonX               float64 `json:"moon_x_m"`
	MoonY               float64 `json:"moon_y_m"`
	Phase               string  `json:"mission_phase"`
	BatterySOC          float64 `json:"battery_soc"`
	ThermalTemperatureC float64 `json:"thermal_temperature_c"`
	ThermalMargin       float64 `json:"thermal_margin"`
	PropellantFraction  float64 `json:"propellant_fraction"`
	ThrustFraction      float64 `json:"thrust_fraction"`
	EngineFiring        bool    `json:"engine_firing"`
	RCSFiring           bool    `json:"rcs_firing"`
	PowerStatus         string  `json:"power_status"`
	PropulsionStatus    string  `json:"propulsion_status"`
	ThermalStatus       string  `json:"thermal_status"`
	CommunicationStatus string  `json:"communication_status"`
	NavigationStatus    string  `json:"navigation_status"`
	HealthStatus        string  `json:"health_status"`
	Message             string  `json:"message,omitempty"`
}

type physicalMissionRuntimeBridge struct {
	mu       sync.Mutex
	cmd      *exec.Cmd
	stdin    io.WriteCloser
	output   *bufio.Reader
	snapshot PhysicalMissionRuntimeSnapshot
}

func NewPhysicalMissionRuntimeBridge(path string) (*physicalMissionRuntimeBridge, error) {
	path = strings.TrimSpace(path)
	if path == "" {
		return nil, fmt.Errorf("TRISHULA_PHYSICAL_MISSION_RUNTIME_PATH is required")
	}
	if !filepath.IsAbs(path) {
		if abs, err := filepath.Abs(path); err == nil {
			path = abs
		}
	}
	if _, err := os.Stat(path); err != nil {
		return nil, fmt.Errorf("runtime bridge %q is not available: %w", path, err)
	}
	cmd := exec.Command(path)
	if runtimePath := strings.TrimSpace(os.Getenv("TRISHULA_VEHICLE_BRIDGE_RUNTIME_PATH")); runtimePath != "" {
		cmd.Env = append(os.Environ(), "PATH="+runtimePath+string(os.PathListSeparator)+os.Getenv("PATH"))
	} else if runtimePath := defaultVehicleBridgeRuntimePath(path); runtimePath != "" {
		cmd.Env = append(os.Environ(), "PATH="+runtimePath+string(os.PathListSeparator)+os.Getenv("PATH"))
	}
	stdin, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		_ = stdin.Close()
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		_ = stdin.Close()
		return nil, err
	}
	b := &physicalMissionRuntimeBridge{cmd: cmd, stdin: stdin, output: bufio.NewReader(stdout)}
	if err := b.commandLocked("STATUS"); err != nil {
		_ = b.Close()
		return nil, err
	}
	return b, nil
}

func parseRuntimeSnapshot(parts []string) (PhysicalMissionRuntimeSnapshot, error) {
	if len(parts) < 33 {
		return PhysicalMissionRuntimeSnapshot{}, fmt.Errorf("runtime bridge returned %d fields; expected at least 33", len(parts))
	}
	u := func(i int) (uint64, error) { return strconv.ParseUint(parts[i], 10, 64) }
	f := func(i int) (float64, error) { return strconv.ParseFloat(parts[i], 64) }
	s := PhysicalMissionRuntimeSnapshot{Result: parts[1], State: parts[2]}
	var err error
	if s.ControlSequence, err = u(3); err != nil {
		return s, err
	}
	if s.TelemetrySequence, err = u(4); err != nil {
		return s, err
	}
	vals := []*float64{&s.MissionTimeSeconds, &s.PhaseElapsedSeconds, &s.PhaseProgress, &s.PositionX, &s.PositionY, &s.PositionZ, &s.VelocityX, &s.VelocityY, &s.VelocityZ, &s.Altitude, &s.Speed, &s.DistanceToMoon, &s.MoonX, &s.MoonY}
	for j, p := range vals {
		if *p, err = f(5 + j); err != nil {
			return s, err
		}
	}
	s.Phase = parts[19]
	if s.BatterySOC, err = f(20); err != nil {
		return s, err
	}
	if s.ThermalTemperatureC, err = f(21); err != nil {
		return s, err
	}
	if s.ThermalMargin, err = f(22); err != nil {
		return s, err
	}
	if s.PropellantFraction, err = f(23); err != nil {
		return s, err
	}
	if s.ThrustFraction, err = f(24); err != nil {
		return s, err
	}
	if s.EngineFiring, err = strconv.ParseBool(parts[25]); err != nil {
		return s, err
	}
	if s.RCSFiring, err = strconv.ParseBool(parts[26]); err != nil {
		return s, err
	}
	s.PowerStatus = parts[27]
	s.PropulsionStatus = parts[28]
	s.ThermalStatus = parts[29]
	s.CommunicationStatus = parts[30]
	s.NavigationStatus = parts[31]
	s.HealthStatus = parts[32]
	if len(parts) > 33 {
		s.Message = strings.Join(parts[33:], "\t")
	}
	return s, nil
}

func (b *physicalMissionRuntimeBridge) commandLocked(command string) error {
	if b == nil || b.cmd == nil {
		return fmt.Errorf("physical mission runtime bridge is not running")
	}
	if _, err := io.WriteString(b.stdin, command+"\n"); err != nil {
		return err
	}
	line, err := b.output.ReadString('\n')
	if err != nil {
		return err
	}
	parts := strings.Split(strings.TrimRight(line, "\r\n"), "\t")
	snap, err := parseRuntimeSnapshot(parts)
	if err != nil {
		return err
	}
	b.snapshot = snap
	if snap.Result == "ERROR" {
		return fmt.Errorf("physical mission runtime: %s", snap.Message)
	}
	return nil
}
func (b *physicalMissionRuntimeBridge) Command(command string) (PhysicalMissionRuntimeSnapshot, error) {
	b.mu.Lock()
	defer b.mu.Unlock()
	err := b.commandLocked(command)
	return b.snapshot, err
}
func (b *physicalMissionRuntimeBridge) Snapshot() PhysicalMissionRuntimeSnapshot {
	b.mu.Lock()
	defer b.mu.Unlock()
	return b.snapshot
}
func (b *physicalMissionRuntimeBridge) Close() error {
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.stdin == nil {
		return nil
	}
	_ = b.stdin.Close()
	err := b.cmd.Wait()
	b.stdin = nil
	return err
}

func physicalMissionRuntimePath() string {
	if v := strings.TrimSpace(os.Getenv("TRISHULA_PHYSICAL_MISSION_RUNTIME_PATH")); v != "" {
		return v
	}
	return filepath.Join("..", "..", "build", "trishula_physical_mission_runtime_bridge.exe")
}

func runtimeTelemetryRecords(missionID string, s PhysicalMissionRuntimeSnapshot) []GroundRecord {
	metrics := []struct{ name, subsystem, unit, value string }{
		{"position_x_m", "navigation", "m", strconv.FormatFloat(s.PositionX, 'g', -1, 64)},
		{"position_y_m", "navigation", "m", strconv.FormatFloat(s.PositionY, 'g', -1, 64)},
		{"position_z_m", "navigation", "m", strconv.FormatFloat(s.PositionZ, 'g', -1, 64)},
		{"velocity_x_m_per_s", "navigation", "m/s", strconv.FormatFloat(s.VelocityX, 'g', -1, 64)},
		{"velocity_y_m_per_s", "navigation", "m/s", strconv.FormatFloat(s.VelocityY, 'g', -1, 64)},
		{"velocity_z_m_per_s", "navigation", "m/s", strconv.FormatFloat(s.VelocityZ, 'g', -1, 64)},
		{"altitude_m", "navigation", "m", strconv.FormatFloat(s.Altitude, 'g', -1, 64)},
		{"speed_m_per_s", "navigation", "m/s", strconv.FormatFloat(s.Speed, 'g', -1, 64)},
		{"distance_to_moon_m", "navigation", "m", strconv.FormatFloat(s.DistanceToMoon, 'g', -1, 64)},
		{"moon_x_m", "ephemeris", "m", strconv.FormatFloat(s.MoonX, 'g', -1, 64)},
		{"moon_y_m", "ephemeris", "m", strconv.FormatFloat(s.MoonY, 'g', -1, 64)},
		{"mission_time_seconds", "mission", "s", strconv.FormatFloat(s.MissionTimeSeconds, 'g', -1, 64)},
		{"phase_elapsed_seconds", "mission", "s", strconv.FormatFloat(s.PhaseElapsedSeconds, 'g', -1, 64)},
		{"phase_progress", "mission", "ratio", strconv.FormatFloat(s.PhaseProgress, 'g', -1, 64)},
		{"battery_soc", "power", "ratio", strconv.FormatFloat(s.BatterySOC, 'g', -1, 64)},
		{"thermal_temperature_c", "thermal", "C", strconv.FormatFloat(s.ThermalTemperatureC, 'g', -1, 64)},
		{"thermal_margin", "thermal", "ratio", strconv.FormatFloat(s.ThermalMargin, 'g', -1, 64)},
		{"propellant_fraction", "propulsion", "ratio", strconv.FormatFloat(s.PropellantFraction, 'g', -1, 64)},
		{"thrust_fraction", "propulsion", "ratio", strconv.FormatFloat(s.ThrustFraction, 'g', -1, 64)},
	}
	records := make([]GroundRecord, 0, len(metrics))
	now := uint64(time.Now().UTC().UnixNano())
	for i, m := range metrics {
		id := fmt.Sprintf("PHYS-RUNTIME-%d-%d", now, i)
		records = append(records, GroundRecord{Envelope: GroundEnvelope{SchemaVersion: 1, Kind: KindTelemetry, RecordID: id, MissionID: missionID, SourceNode: "SPACECRAFT-01", OriginNode: "SPACECRAFT-01", DestinationNode: "GS-TRISHULA-01", Priority: "normal", ApplicationID: 101, MissionTimestamp: now, SequenceNumber: s.TelemetrySequence*100 + uint64(i+1), Quality: 1, CorrelationID: fmt.Sprintf("PHYS-RUNTIME-TICK-%d", s.TelemetrySequence), PayloadSchema: "trishula.physical.telemetry.v0.9.112"}, Fields: map[string]string{"metric": m.name, "value": m.value, "unit": m.unit, "subsystem": m.subsystem, "quality": "1", "mission_phase": s.Phase, "phase_elapsed_seconds": strconv.FormatFloat(s.PhaseElapsedSeconds, 'g', -1, 64), "phase_progress": strconv.FormatFloat(s.PhaseProgress, 'g', -1, 64), "physical_telemetry": "true"}})
	}
	return records
}

func startPhysicalMissionRuntimeTicker(ctx context.Context, bridge *physicalMissionRuntimeBridge, service *Service, missionID string, interval time.Duration) {
	go func() {
		ticker := time.NewTicker(interval)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				snap := bridge.Snapshot()
				if snap.State != "RUNNING" {
					continue
				}
				next, err := bridge.Command("STEP")
				if err != nil {
					continue
				}
				for _, record := range runtimeTelemetryRecords(missionID, next) {
					_ = service.Ingest(record)
				}
			}
		}
	}()
}
