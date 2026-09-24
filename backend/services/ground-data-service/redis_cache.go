package main

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"strconv"
	"strings"
	"sync"
	"time"
)

const redisStateKey = "trishula:ground:current-state"

// RedisStateCache stores the latest projected ground state as one JSON document.
// It deliberately uses the Redis RESP2 protocol directly to keep the simulator
// dependency-light while still exercising a real Redis server.
type RedisStateCache struct {
	addr    string
	timeout time.Duration
	mu      sync.Mutex
}

func NewRedisStateCache(addr string) *RedisStateCache {
	return &RedisStateCache{addr: addr, timeout: 3 * time.Second}
}

func (r *RedisStateCache) dial(ctx context.Context) (net.Conn, error) {
	if strings.TrimSpace(r.addr) == "" {
		return nil, errors.New("redis address is required")
	}
	timeout := r.timeout
	if deadline, ok := ctx.Deadline(); ok {
		remaining := time.Until(deadline)
		if remaining > 0 && remaining < timeout {
			timeout = remaining
		}
	}
	d := net.Dialer{Timeout: timeout}
	conn, err := d.DialContext(ctx, "tcp", r.addr)
	if err != nil {
		return nil, fmt.Errorf("dial redis: %w", err)
	}
	_ = conn.SetDeadline(time.Now().Add(timeout))
	return conn, nil
}

func respCommand(args ...string) []byte {
	var b strings.Builder
	b.WriteString("*" + strconv.Itoa(len(args)) + "\r\n")
	for _, arg := range args {
		b.WriteString("$" + strconv.Itoa(len(arg)) + "\r\n")
		b.WriteString(arg)
		b.WriteString("\r\n")
	}
	return []byte(b.String())
}

func readRESP(br *bufio.Reader) (any, error) {
	prefix, err := br.ReadByte()
	if err != nil {
		return nil, err
	}
	line, err := br.ReadString('\n')
	if err != nil {
		return nil, err
	}
	line = strings.TrimSuffix(strings.TrimSuffix(line, "\n"), "\r")
	switch prefix {
	case '+':
		return line, nil
	case '-':
		return nil, errors.New(line)
	case ':':
		v, err := strconv.ParseInt(line, 10, 64)
		return v, err
	case '$':
		n, err := strconv.Atoi(line)
		if err != nil {
			return nil, err
		}
		if n < 0 {
			return nil, nil
		}
		data := make([]byte, n+2)
		if _, err := ioReadFull(br, data); err != nil {
			return nil, err
		}
		return string(data[:n]), nil
	default:
		return nil, fmt.Errorf("unsupported RESP prefix %q", prefix)
	}
}

func ioReadFull(br *bufio.Reader, buf []byte) (int, error) {
	total := 0
	for total < len(buf) {
		n, err := br.Read(buf[total:])
		total += n
		if err != nil {
			return total, err
		}
	}
	return total, nil
}

func (r *RedisStateCache) do(ctx context.Context, args ...string) (any, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	conn, err := r.dial(ctx)
	if err != nil {
		return nil, err
	}
	defer conn.Close()
	if _, err := conn.Write(respCommand(args...)); err != nil {
		return nil, fmt.Errorf("write redis command: %w", err)
	}
	return readRESP(bufio.NewReader(conn))
}

func (r *RedisStateCache) SetSnapshot(ctx context.Context, state CurrentState) error {
	payload, err := json.Marshal(state)
	if err != nil {
		return fmt.Errorf("encode redis state: %w", err)
	}
	if _, err := r.do(ctx, "SET", redisStateKey, string(payload)); err != nil {
		return fmt.Errorf("set redis state: %w", err)
	}
	return nil
}

func (r *RedisStateCache) GetSnapshot(ctx context.Context) (CurrentState, bool, error) {
	value, err := r.do(ctx, "GET", redisStateKey)
	if err != nil {
		return CurrentState{}, false, fmt.Errorf("get redis state: %w", err)
	}
	if value == nil {
		return CurrentState{}, false, nil
	}
	raw, ok := value.(string)
	if !ok {
		return CurrentState{}, false, fmt.Errorf("unexpected redis GET type %T", value)
	}
	var state CurrentState
	if err := json.Unmarshal([]byte(raw), &state); err != nil {
		return CurrentState{}, false, fmt.Errorf("decode redis state: %w", err)
	}
	return state, true, nil
}

func (r *RedisStateCache) Close() error { return nil }
