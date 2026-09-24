package main

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestPostgresMigrationArtifactsExist(t *testing.T) {
	root := filepath.Join("..", "..", "db", "migrations")
	entries, err := os.ReadDir(root)
	if err != nil {
		t.Fatalf("read migrations: %v", err)
	}
	if len(entries) < 2 {
		t.Fatalf("expected at least two migration files, found %d", len(entries))
	}
	for _, entry := range entries {
		if entry.IsDir() || !strings.HasSuffix(entry.Name(), ".sql") {
			continue
		}
		raw, err := os.ReadFile(filepath.Join(root, entry.Name()))
		if err != nil {
			t.Fatalf("read migration %s: %v", entry.Name(), err)
		}
		text := string(raw)
		required := []string{"ground_records"}
		if strings.Contains(entry.Name(), "001_") {
			required = append(required, "record_id", "mission_timestamp_ns")
		}
		for _, field := range required {
			if !strings.Contains(text, field) {
				t.Fatalf("migration %s missing %q", entry.Name(), field)
			}
		}
	}
}
