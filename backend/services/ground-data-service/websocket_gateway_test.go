package main

import (
	"bufio"
	"bytes"
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestWebSocketAcceptKey(t *testing.T) {
	key := "dGhlIHNhbXBsZSBub25jZQ=="
	hash := sha1.Sum([]byte(key + webSocketGUID))
	want := base64.StdEncoding.EncodeToString(hash[:])
	if got := webSocketAcceptKey(key); got != want {
		t.Fatalf("accept key = %q, want %q", got, want)
	}
}

func TestIsWebSocketUpgrade(t *testing.T) {
	r := httptest.NewRequest(http.MethodGet, "/v1/mission-control/ws", nil)
	r.Header.Set("Upgrade", "websocket")
	r.Header.Set("Connection", "keep-alive, Upgrade")
	if !isWebSocketUpgrade(r) {
		t.Fatal("expected websocket upgrade")
	}
}

func TestWebSocketGatewayRejectsNonUpgrade(t *testing.T) {
	gateway := NewMissionControlWebSocketGateway()
	r := httptest.NewRequest(http.MethodGet, "/v1/mission-control/ws", nil)
	w := httptest.NewRecorder()
	gateway.ServeHTTP(w, r)
	if w.Code != http.StatusBadRequest {
		t.Fatalf("status = %d", w.Code)
	}
	if !strings.Contains(w.Body.String(), "websocket upgrade required") {
		t.Fatalf("body = %s", w.Body.String())
	}
}

func TestWebSocketGatewayMethod(t *testing.T) {
	gateway := NewMissionControlWebSocketGateway()
	r := httptest.NewRequest(http.MethodPost, "/v1/mission-control/ws", bytes.NewReader(nil))
	w := httptest.NewRecorder()
	gateway.ServeHTTP(w, r)
	if w.Code != http.StatusMethodNotAllowed {
		t.Fatalf("status = %d", w.Code)
	}
}

func TestWebSocketGatewayClientCount(t *testing.T) {
	gateway := NewMissionControlWebSocketGateway()
	if got := gateway.ClientCount(); got != 0 {
		t.Fatalf("initial clients = %d", got)
	}
}

func TestWebSocketTextFrameEncoding(t *testing.T) {
	left, right := netPipe(t)
	client := &missionControlWebSocketClient{conn: left}
	payload := []byte(`{"type":"test"}`)
	go func() { _ = client.writeText(payload) }()
	reader := bufio.NewReader(right)
	opcode, got, err := readWebSocketFrameForTest(reader)
	if err != nil {
		t.Fatal(err)
	}
	if opcode != 0x1 || !bytes.Equal(got, payload) {
		t.Fatalf("frame = opcode %d payload %q", opcode, got)
	}
	_ = right.Close()
}

func netPipe(t *testing.T) (net.Conn, net.Conn) {
	t.Helper()
	return net.Pipe()
}

func readWebSocketFrameForTest(r *bufio.Reader) (byte, []byte, error) {
	first, err := r.ReadByte()
	if err != nil {
		return 0, nil, err
	}
	second, err := r.ReadByte()
	if err != nil {
		return 0, nil, err
	}
	length := int(second & 0x7f)
	if length == 126 {
		var b [2]byte
		if _, err := io.ReadFull(r, b[:]); err != nil {
			return 0, nil, err
		}
		length = int(binary.BigEndian.Uint16(b[:]))
	} else if length == 127 {
		var b [8]byte
		if _, err := io.ReadFull(r, b[:]); err != nil {
			return 0, nil, err
		}
		length = int(binary.BigEndian.Uint64(b[:]))
	}
	payload := make([]byte, length)
	_, err = io.ReadFull(r, payload)
	return first & 0x0f, payload, err
}
