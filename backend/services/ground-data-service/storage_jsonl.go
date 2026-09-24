//go:build !postgres

package main

import "context"

func openGroundStore(_ context.Context, storagePath string) (RecordStore, string, error) {
	store, err := NewJSONLStore(storagePath)
	if err != nil {
		return nil, "jsonl", err
	}
	return store, "jsonl:" + storagePath, nil
}
