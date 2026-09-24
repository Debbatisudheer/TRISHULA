package main

import (
	"bufio"
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

const (
	currentWebSocketGatewayVersion = "v0.9.102"
	currentTelemetryStreamVersion  = "v0.9.99"
	currentCommandStreamVersion    = "v0.9.100"
	currentAlertStreamVersion      = "v0.9.102"
	webSocketGUID                  = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
	webSocketWriteTimeout          = 5 * time.Second
)

type MissionControlWebSocketGateway struct {
	mu      sync.RWMutex
	clients map[*missionControlWebSocketClient]struct{}
}

type missionControlWebSocketClient struct {
	conn net.Conn
	mu   sync.Mutex
}

type missionControlWebSocketEnvelope struct {
	Type            string `json:"type"`
	GatewayVersion  string `json:"gateway_version"`
	ProtocolVersion string `json:"protocol_version"`
	ConnectedAt     string `json:"connected_at,omitempty"`
	Data            any    `json:"data,omitempty"`
}

func NewMissionControlWebSocketGateway() *MissionControlWebSocketGateway {
	return &MissionControlWebSocketGateway{clients: make(map[*missionControlWebSocketClient]struct{})}
}

func (g *MissionControlWebSocketGateway) ClientCount() int {
	if g == nil {
		return 0
	}
	g.mu.RLock()
	defer g.mu.RUnlock()
	return len(g.clients)
}

func (g *MissionControlWebSocketGateway) register(client *missionControlWebSocketClient) {
	g.mu.Lock()
	defer g.mu.Unlock()
	g.clients[client] = struct{}{}
}

func (g *MissionControlWebSocketGateway) unregister(client *missionControlWebSocketClient) {
	g.mu.Lock()
	defer g.mu.Unlock()
	delete(g.clients, client)
}

// PublishJSON broadcasts a complete JSON text message to all connected clients.
// V0.9.98 establishes the transport; domain-specific live event publication is
// intentionally added in later Mission Control milestones.
func (g *MissionControlWebSocketGateway) PublishJSON(payload []byte) {
	if g == nil || len(payload) == 0 {
		return
	}
	g.mu.RLock()
	clients := make([]*missionControlWebSocketClient, 0, len(g.clients))
	for client := range g.clients {
		clients = append(clients, client)
	}
	g.mu.RUnlock()

	for _, client := range clients {
		if err := client.writeText(payload); err != nil {
			g.unregister(client)
			_ = client.conn.Close()
		}
	}
}

type missionControlTelemetryEnvelope struct {
	Type             string            `json:"type"`
	StreamVersion    string            `json:"stream_version"`
	ProtocolVersion  string            `json:"protocol_version"`
	MissionID        string            `json:"mission_id"`
	SourceNode       string            `json:"source_node"`
	RecordID         string            `json:"record_id"`
	SequenceNumber   uint64            `json:"sequence_number"`
	MissionTimestamp uint64            `json:"mission_timestamp_ns"`
	CorrelationID    string            `json:"correlation_id,omitempty"`
	Fields           map[string]string `json:"fields"`
}

// PublishTelemetry publishes an accepted telemetry record to every connected
// Mission Control client. The Kafka consumer remains responsible for
// validation, routing, persistence, and state derivation; this method only
// projects the already-accepted record onto the real-time client stream.

type missionControlCommandExecutionEnvelope struct {
	Type             string            `json:"type"`
	StreamVersion    string            `json:"stream_version"`
	ProtocolVersion  string            `json:"protocol_version"`
	MissionID        string            `json:"mission_id"`
	SourceNode       string            `json:"source_node"`
	RecordID         string            `json:"record_id"`
	SequenceNumber   uint64            `json:"sequence_number"`
	MissionTimestamp uint64            `json:"mission_timestamp_ns"`
	CorrelationID    string            `json:"correlation_id,omitempty"`
	CommandID        string            `json:"command_id"`
	Command          string            `json:"command,omitempty"`
	Target           string            `json:"target,omitempty"`
	Lifecycle        string            `json:"lifecycle,omitempty"`
	ExecutionState   string            `json:"execution_state,omitempty"`
	EventType        string            `json:"event_type,omitempty"`
	VehicleResponse  string            `json:"vehicle_response,omitempty"`
	Severity         string            `json:"severity,omitempty"`
	Fields           map[string]string `json:"fields"`
}

// PublishCommandExecution projects accepted command lifecycle/execution events
// onto the Mission Control real-time stream. Persistence, Kafka delivery, and
// command reconciliation remain owned by their existing boundaries.
func (g *MissionControlWebSocketGateway) PublishCommandExecution(record GroundRecord) {
	if g == nil || (record.Envelope.Kind != KindCommand && record.Envelope.Kind != KindEvent) {
		return
	}
	commandID := strings.TrimSpace(record.Fields["command_id"])
	if commandID == "" {
		commandID = resolveCommandID(record)
	}
	if commandID == "" {
		return
	}
	if record.Envelope.Kind == KindEvent && !strings.EqualFold(strings.TrimSpace(record.Fields["event_class"]), "COMMAND") {
		return
	}
	fields := make(map[string]string, len(record.Fields))
	for key, value := range record.Fields {
		fields[key] = value
	}
	payload, err := json.Marshal(missionControlCommandExecutionEnvelope{
		Type:             "mission_control.command_execution",
		StreamVersion:    currentCommandStreamVersion,
		ProtocolVersion:  "trishula-ws-v1",
		MissionID:        record.Envelope.MissionID,
		SourceNode:       record.Envelope.SourceNode,
		RecordID:         record.Envelope.RecordID,
		SequenceNumber:   record.Envelope.SequenceNumber,
		MissionTimestamp: record.Envelope.MissionTimestamp,
		CorrelationID:    record.Envelope.CorrelationID,
		CommandID:        commandID,
		Command:          record.Fields["command"],
		Target:           record.Fields["target"],
		Lifecycle:        record.Fields["lifecycle"],
		ExecutionState:   record.Fields["execution_state"],
		EventType:        record.Fields["event_type"],
		VehicleResponse:  record.Fields["vehicle_response"],
		Severity:         record.Fields["severity"],
		Fields:           fields,
	})
	if err != nil {
		return
	}
	g.PublishJSON(payload)
}

type missionControlAlertEnvelope struct {
	Type             string `json:"type"`
	StreamVersion    string `json:"stream_version"`
	ProtocolVersion  string `json:"protocol_version"`
	MissionID        string `json:"mission_id"`
	SourceNode       string `json:"source_node"`
	RecordID         string `json:"record_id"`
	SequenceNumber   uint64 `json:"sequence_number"`
	MissionTimestamp uint64 `json:"mission_timestamp_ns"`
	CorrelationID    string `json:"correlation_id,omitempty"`
	AlertID          string `json:"alert_id"`
	EventType        string `json:"event_type"`
	Severity         string `json:"severity"`
	Lifecycle        string `json:"lifecycle"`
	Status           string `json:"status"`
	Subsystem        string `json:"subsystem,omitempty"`
	FaultCode        string `json:"fault_code,omitempty"`
	AlertKey         string `json:"alert_key"`
	Message          string `json:"message"`
}

func (g *MissionControlWebSocketGateway) PublishAlert(alert MissionControlAlert) {
	if g == nil {
		return
	}
	payload, err := json.Marshal(missionControlAlertEnvelope{
		Type: "mission_control.alert", StreamVersion: currentAlertStreamVersion, ProtocolVersion: "trishula-ws-v1",
		MissionID: alert.MissionID, SourceNode: alert.SourceNode, RecordID: alert.RecordID, SequenceNumber: alert.SequenceNumber,
		MissionTimestamp: alert.MissionTimestamp, CorrelationID: alert.CorrelationID, AlertID: alert.AlertID, EventType: alert.EventType,
		Severity: alert.Severity, Lifecycle: alert.Lifecycle, Status: alert.Status, Subsystem: alert.Subsystem, FaultCode: alert.FaultCode,
		AlertKey: alert.AlertKey, Message: alert.Message,
	})
	if err != nil {
		return
	}
	g.PublishJSON(payload)
}

func (g *MissionControlWebSocketGateway) PublishTelemetry(record GroundRecord) {
	if g == nil || record.Envelope.Kind != KindTelemetry {
		return
	}
	fields := make(map[string]string, len(record.Fields))
	for key, value := range record.Fields {
		fields[key] = value
	}
	payload, err := json.Marshal(missionControlTelemetryEnvelope{
		Type:             "mission_control.telemetry",
		StreamVersion:    currentTelemetryStreamVersion,
		ProtocolVersion:  "trishula-ws-v1",
		MissionID:        record.Envelope.MissionID,
		SourceNode:       record.Envelope.SourceNode,
		RecordID:         record.Envelope.RecordID,
		SequenceNumber:   record.Envelope.SequenceNumber,
		MissionTimestamp: record.Envelope.MissionTimestamp,
		CorrelationID:    record.Envelope.CorrelationID,
		Fields:           fields,
	})
	if err != nil {
		return
	}
	g.PublishJSON(payload)
}

func (g *MissionControlWebSocketGateway) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if g == nil {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "websocket gateway unavailable"})
		return
	}
	if r.Method != http.MethodGet {
		writeJSON(w, http.StatusMethodNotAllowed, map[string]string{"error": "method not allowed"})
		return
	}
	if !isWebSocketUpgrade(r) {
		w.Header().Set("Upgrade", "websocket")
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "websocket upgrade required"})
		return
	}
	key := strings.TrimSpace(r.Header.Get("Sec-WebSocket-Key"))
	if key == "" {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "Sec-WebSocket-Key is required"})
		return
	}

	hijacker, ok := w.(http.Hijacker)
	if !ok {
		writeJSON(w, http.StatusInternalServerError, map[string]string{"error": "websocket hijacking is unsupported"})
		return
	}
	conn, rw, err := hijacker.Hijack()
	if err != nil {
		return
	}
	accept := webSocketAcceptKey(key)
	_, err = fmt.Fprintf(rw, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\nX-TRISHULA-API-VERSION: %s\r\n\r\n", accept, currentWebSocketGatewayVersion)
	if err != nil {
		_ = conn.Close()
		return
	}
	if err := rw.Flush(); err != nil {
		_ = conn.Close()
		return
	}

	client := &missionControlWebSocketClient{conn: conn}
	g.register(client)
	defer func() {
		g.unregister(client)
		_ = conn.Close()
	}()

	welcome, _ := json.Marshal(missionControlWebSocketEnvelope{
		Type:            "mission_control.websocket.connected",
		GatewayVersion:  currentWebSocketGatewayVersion,
		ProtocolVersion: "trishula-ws-v1",
		ConnectedAt:     time.Now().UTC().Format(time.RFC3339Nano),
	})
	if err := client.writeText(welcome); err != nil {
		return
	}

	_ = conn.SetReadDeadline(time.Now().Add(2 * time.Minute))
	reader := bufio.NewReader(conn)
	for {
		opcode, payload, err := readWebSocketFrame(reader)
		if err != nil {
			return
		}
		switch opcode {
		case 0x8: // close
			_ = client.writeClose()
			return
		case 0x9: // ping
			if err := client.writeFrame(0xA, payload); err != nil {
				return
			}
		case 0xA: // pong
		default:
			// Client application messages are intentionally ignored in V0.9.98.
		}
		_ = conn.SetReadDeadline(time.Now().Add(2 * time.Minute))
	}
}

func isWebSocketUpgrade(r *http.Request) bool {
	if r == nil {
		return false
	}
	upgrade := strings.EqualFold(strings.TrimSpace(r.Header.Get("Upgrade")), "websocket")
	connection := false
	for _, value := range r.Header.Values("Connection") {
		for _, token := range strings.Split(value, ",") {
			if strings.EqualFold(strings.TrimSpace(token), "upgrade") {
				connection = true
				break
			}
		}
	}
	return upgrade && connection
}

func webSocketAcceptKey(key string) string {
	hash := sha1.Sum([]byte(key + webSocketGUID))
	return base64.StdEncoding.EncodeToString(hash[:])
}

func (c *missionControlWebSocketClient) writeText(payload []byte) error {
	return c.writeFrame(0x1, payload)
}

func (c *missionControlWebSocketClient) writeClose() error {
	return c.writeFrame(0x8, nil)
}

func (c *missionControlWebSocketClient) writeFrame(opcode byte, payload []byte) error {
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(payload) > int(^uint32(0)) {
		return errors.New("websocket payload too large")
	}
	frame := make([]byte, 0, len(payload)+14)
	frame = append(frame, 0x80|(opcode&0x0f))
	switch {
	case len(payload) <= 125:
		frame = append(frame, byte(len(payload)))
	case len(payload) <= 65535:
		frame = append(frame, 126, byte(len(payload)>>8), byte(len(payload)))
	default:
		frame = append(frame, 127)
		var length [8]byte
		binary.BigEndian.PutUint64(length[:], uint64(len(payload)))
		frame = append(frame, length[:]...)
	}
	frame = append(frame, payload...)
	_ = c.conn.SetWriteDeadline(time.Now().Add(webSocketWriteTimeout))
	_, err := c.conn.Write(frame)
	return err
}

func readWebSocketFrame(r *bufio.Reader) (byte, []byte, error) {
	first, err := r.ReadByte()
	if err != nil {
		return 0, nil, err
	}
	second, err := r.ReadByte()
	if err != nil {
		return 0, nil, err
	}
	fin := first&0x80 != 0
	opcode := first & 0x0f
	masked := second&0x80 != 0
	if !fin || !masked {
		return 0, nil, errors.New("fragmented or unmasked client websocket frame")
	}
	length := int(second & 0x7f)
	switch length {
	case 126:
		var extended [2]byte
		if _, err := io.ReadFull(r, extended[:]); err != nil {
			return 0, nil, err
		}
		length = int(binary.BigEndian.Uint16(extended[:]))
	case 127:
		var extended [8]byte
		if _, err := io.ReadFull(r, extended[:]); err != nil {
			return 0, nil, err
		}
		value := binary.BigEndian.Uint64(extended[:])
		if value > uint64(^uint(0)>>1) {
			return 0, nil, errors.New("websocket frame too large")
		}
		length = int(value)
	}
	var mask [4]byte
	if _, err := io.ReadFull(r, mask[:]); err != nil {
		return 0, nil, err
	}
	payload := make([]byte, length)
	if _, err := io.ReadFull(r, payload); err != nil {
		return 0, nil, err
	}
	for i := range payload {
		payload[i] ^= mask[i%4]
	}
	return opcode, payload, nil
}
