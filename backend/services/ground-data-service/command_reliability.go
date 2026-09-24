package main

import (
	"os"
	"strconv"
	"strings"
	"time"
)

// CommandReliabilityPolicy defines bounded per-attempt timeout and retry behavior.
// It is intentionally local to execution orchestration so existing validation and
// reconciliation state machines remain unchanged.
type CommandReliabilityPolicy struct {
	MaxAttempts    int
	AttemptTimeout time.Duration
	InitialBackoff time.Duration
	MaxBackoff     time.Duration
}

func DefaultCommandReliabilityPolicy() CommandReliabilityPolicy {
	return CommandReliabilityPolicy{MaxAttempts: 3, AttemptTimeout: 10 * time.Second, InitialBackoff: 250 * time.Millisecond, MaxBackoff: 2 * time.Second}
}

func normalizeCommandReliabilityPolicy(p CommandReliabilityPolicy) CommandReliabilityPolicy {
	defaults := DefaultCommandReliabilityPolicy()
	if p.MaxAttempts <= 0 {
		p.MaxAttempts = defaults.MaxAttempts
	}
	if p.AttemptTimeout <= 0 {
		p.AttemptTimeout = defaults.AttemptTimeout
	}
	if p.InitialBackoff <= 0 {
		p.InitialBackoff = defaults.InitialBackoff
	}
	if p.MaxBackoff <= 0 {
		p.MaxBackoff = defaults.MaxBackoff
	}
	if p.MaxBackoff < p.InitialBackoff {
		p.MaxBackoff = p.InitialBackoff
	}
	return p
}

func (p CommandReliabilityPolicy) Backoff(attempt int) time.Duration {
	if attempt <= 0 {
		return p.InitialBackoff
	}
	d := p.InitialBackoff
	for i := 1; i < attempt && d < p.MaxBackoff; i++ {
		d *= 2
		if d > p.MaxBackoff {
			d = p.MaxBackoff
		}
	}
	return d
}

// CommandReliabilityPolicyFromEnv loads optional operator controls while preserving
// safe defaults when values are absent or malformed.
func CommandReliabilityPolicyFromEnv() CommandReliabilityPolicy {
	p := DefaultCommandReliabilityPolicy()
	if v := strings.TrimSpace(os.Getenv("TRISHULA_COMMAND_MAX_ATTEMPTS")); v != "" {
		if n, err := strconv.Atoi(v); err == nil {
			p.MaxAttempts = n
		}
	}
	if v := strings.TrimSpace(os.Getenv("TRISHULA_COMMAND_ATTEMPT_TIMEOUT")); v != "" {
		if d, err := time.ParseDuration(v); err == nil {
			p.AttemptTimeout = d
		}
	}
	if v := strings.TrimSpace(os.Getenv("TRISHULA_COMMAND_INITIAL_BACKOFF")); v != "" {
		if d, err := time.ParseDuration(v); err == nil {
			p.InitialBackoff = d
		}
	}
	if v := strings.TrimSpace(os.Getenv("TRISHULA_COMMAND_MAX_BACKOFF")); v != "" {
		if d, err := time.ParseDuration(v); err == nil {
			p.MaxBackoff = d
		}
	}
	return normalizeCommandReliabilityPolicy(p)
}
