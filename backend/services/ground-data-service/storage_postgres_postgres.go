//go:build postgres

package main

import (
	"context"
	"errors"
	"os"
)

func openGroundStore(ctx context.Context, _ string) (RecordStore, string, error) {
	dsn := os.Getenv("TRISHULA_GROUND_DB_URL")
	if dsn == "" {
		return nil, "postgres", errors.New("TRISHULA_GROUND_DB_URL is required when built with -tags postgres")
	}
	store, err := OpenPostgresStore(ctx, dsn)
	if err != nil {
		return nil, "postgres", err
	}
	return store, "postgres", nil
}
