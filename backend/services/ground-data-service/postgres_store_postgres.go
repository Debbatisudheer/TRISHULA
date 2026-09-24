//go:build postgres

package main

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"

	_ "github.com/jackc/pgx/v5/stdlib"
)

// PostgresStore is the production-oriented RecordStore implementation.
// Build with -tags postgres after adding the pgx dependency:
//
//	go get github.com/jackc/pgx/v5
//
// JSONL remains the default local-first store so the base project stays
// dependency-light and deterministic for the simulation workflow.
type PostgresStore struct {
	db *sql.DB
}

func NewPostgresStore(db *sql.DB) (*PostgresStore, error) {
	if db == nil {
		return nil, fmt.Errorf("postgres database handle is required")
	}
	return &PostgresStore{db: db}, nil
}

func OpenPostgresStore(ctx context.Context, dsn string) (*PostgresStore, error) {
	if dsn == "" {
		return nil, fmt.Errorf("postgres DSN is required")
	}
	db, err := sql.Open("pgx", dsn)
	if err != nil {
		return nil, fmt.Errorf("open postgres: %w", err)
	}
	if err := db.PingContext(ctx); err != nil {
		_ = db.Close()
		return nil, fmt.Errorf("ping postgres: %w", err)
	}
	return &PostgresStore{db: db}, nil
}

func (s *PostgresStore) Append(record GroundRecord) error {
	fieldsJSON, err := json.Marshal(record.Fields)
	if err != nil {
		return fmt.Errorf("encode fields: %w", err)
	}

	_, err = s.db.Exec(`
        INSERT INTO ground_records (
            record_id, schema_version, kind, mission_id, source_node,
            origin_node, destination_node, priority, application_id,
            mission_timestamp_ns, sequence_number, quality, correlation_id,
            payload_schema, fields_json
        ) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15)
    `,
		record.Envelope.RecordID,
		record.Envelope.SchemaVersion,
		record.Envelope.Kind,
		record.Envelope.MissionID,
		record.Envelope.SourceNode,
		record.Envelope.OriginNode,
		record.Envelope.DestinationNode,
		record.Envelope.Priority,
		record.Envelope.ApplicationID,
		int64(record.Envelope.MissionTimestamp),
		int64(record.Envelope.SequenceNumber),
		record.Envelope.Quality,
		nullIfEmpty(record.Envelope.CorrelationID),
		record.Envelope.PayloadSchema,
		fieldsJSON,
	)
	return err
}

func (s *PostgresStore) LoadAll() ([]GroundRecord, error) {
	rows, err := s.db.Query(`
        SELECT record_id, schema_version, kind, mission_id, source_node,
               origin_node, destination_node, priority, application_id,
               mission_timestamp_ns, sequence_number, quality, correlation_id,
               payload_schema, fields_json
        FROM ground_records
        ORDER BY mission_timestamp_ns ASC, sequence_number ASC, record_id ASC
    `)
	if err != nil {
		return nil, fmt.Errorf("query ground records: %w", err)
	}
	defer rows.Close()

	var records []GroundRecord
	for rows.Next() {
		var record GroundRecord
		var missionTS, sequence int64
		var correlation sql.NullString
		var fieldsJSON []byte
		if err := rows.Scan(
			&record.Envelope.RecordID,
			&record.Envelope.SchemaVersion,
			&record.Envelope.Kind,
			&record.Envelope.MissionID,
			&record.Envelope.SourceNode,
			&record.Envelope.OriginNode,
			&record.Envelope.DestinationNode,
			&record.Envelope.Priority,
			&record.Envelope.ApplicationID,
			&missionTS,
			&sequence,
			&record.Envelope.Quality,
			&correlation,
			&record.Envelope.PayloadSchema,
			&fieldsJSON,
		); err != nil {
			return nil, fmt.Errorf("scan ground record: %w", err)
		}
		if missionTS < 0 || sequence < 0 {
			return nil, fmt.Errorf("negative mission timestamp or sequence in database")
		}
		record.Envelope.MissionTimestamp = uint64(missionTS)
		record.Envelope.SequenceNumber = uint64(sequence)
		if correlation.Valid {
			record.Envelope.CorrelationID = correlation.String
		}
		if err := json.Unmarshal(fieldsJSON, &record.Fields); err != nil {
			return nil, fmt.Errorf("decode fields for %s: %w", record.Envelope.RecordID, err)
		}
		records = append(records, record)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate ground records: %w", err)
	}
	return records, nil
}

func (s *PostgresStore) GetByID(ctx context.Context, recordID string) (GroundRecord, bool, error) {
	row := s.db.QueryRowContext(ctx, `
        SELECT record_id, schema_version, kind, mission_id, source_node,
               origin_node, destination_node, priority, application_id,
               mission_timestamp_ns, sequence_number, quality, correlation_id,
               payload_schema, fields_json
        FROM ground_records
        WHERE record_id = $1
    `, recordID)
	record, err := scanPostgresRecord(row)
	if err == sql.ErrNoRows {
		return GroundRecord{}, false, nil
	}
	if err != nil {
		return GroundRecord{}, false, fmt.Errorf("get ground record %q: %w", recordID, err)
	}
	return record, true, nil
}

func (s *PostgresStore) Query(ctx context.Context, query RecordQuery) ([]GroundRecord, error) {
	if err := validateQuery(query); err != nil {
		return nil, err
	}

	base := `SELECT record_id, schema_version, kind, mission_id, source_node,
               origin_node, destination_node, priority, application_id,
               mission_timestamp_ns, sequence_number, quality, correlation_id,
               payload_schema, fields_json
        FROM ground_records WHERE 1=1`
	args := make([]any, 0, 8)
	appendWhere := func(clause string, value any) {
		args = append(args, value)
		base += fmt.Sprintf(" AND %s = $%d", clause, len(args))
	}
	if query.MissionID != "" {
		appendWhere("mission_id", query.MissionID)
	}
	if query.SourceNode != "" {
		appendWhere("source_node", query.SourceNode)
	}
	if query.Kind != "" {
		appendWhere("kind", query.Kind)
	}
	if query.CorrelationID != "" {
		appendWhere("correlation_id", query.CorrelationID)
	}
	if query.MinMissionTimeNS != nil {
		args = append(args, int64(*query.MinMissionTimeNS))
		base += fmt.Sprintf(" AND mission_timestamp_ns >= $%d", len(args))
	}
	if query.MaxMissionTimeNS != nil {
		args = append(args, int64(*query.MaxMissionTimeNS))
		base += fmt.Sprintf(" AND mission_timestamp_ns <= $%d", len(args))
	}
	if query.MinSequence != nil {
		args = append(args, int64(*query.MinSequence))
		base += fmt.Sprintf(" AND sequence_number >= $%d", len(args))
	}
	if query.MaxSequence != nil {
		args = append(args, int64(*query.MaxSequence))
		base += fmt.Sprintf(" AND sequence_number <= $%d", len(args))
	}
	if query.CursorMissionTimeNS != nil && query.CursorSequence != nil {
		args = append(args, int64(*query.CursorMissionTimeNS))
		t := len(args)
		args = append(args, int64(*query.CursorSequence))
		s := len(args)
		args = append(args, query.CursorRecordID)
		r := len(args)
		base += fmt.Sprintf(" AND (mission_timestamp_ns < $%d OR (mission_timestamp_ns = $%d AND sequence_number < $%d) OR (mission_timestamp_ns = $%d AND sequence_number = $%d AND record_id > $%d))", t, t, s, t, s, r)
	}
	base += " ORDER BY mission_timestamp_ns DESC, sequence_number DESC, record_id ASC"
	if query.Limit > 0 {
		args = append(args, query.Limit)
		base += fmt.Sprintf(" LIMIT $%d", len(args))
	}

	rows, err := s.db.QueryContext(ctx, base, args...)
	if err != nil {
		return nil, fmt.Errorf("query ground records: %w", err)
	}
	defer rows.Close()

	var records []GroundRecord
	for rows.Next() {
		record, err := scanPostgresRows(rows)
		if err != nil {
			return nil, err
		}
		records = append(records, record)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate query results: %w", err)
	}
	return records, nil
}

type postgresScanner interface {
	Scan(dest ...any) error
}

func scanPostgresRecord(row *sql.Row) (GroundRecord, error) {
	return scanPostgresScanner(row)
}

func scanPostgresRows(rows *sql.Rows) (GroundRecord, error) {
	return scanPostgresScanner(rows)
}

func scanPostgresScanner(scanner postgresScanner) (GroundRecord, error) {
	var record GroundRecord
	var missionTS, sequence int64
	var correlation sql.NullString
	var fieldsJSON []byte
	if err := scanner.Scan(
		&record.Envelope.RecordID,
		&record.Envelope.SchemaVersion,
		&record.Envelope.Kind,
		&record.Envelope.MissionID,
		&record.Envelope.SourceNode,
		&record.Envelope.OriginNode,
		&record.Envelope.DestinationNode,
		&record.Envelope.Priority,
		&record.Envelope.ApplicationID,
		&missionTS,
		&sequence,
		&record.Envelope.Quality,
		&correlation,
		&record.Envelope.PayloadSchema,
		&fieldsJSON,
	); err != nil {
		return GroundRecord{}, err
	}
	if missionTS < 0 || sequence < 0 {
		return GroundRecord{}, fmt.Errorf("negative mission timestamp or sequence in database")
	}
	record.Envelope.MissionTimestamp = uint64(missionTS)
	record.Envelope.SequenceNumber = uint64(sequence)
	if correlation.Valid {
		record.Envelope.CorrelationID = correlation.String
	}
	if err := json.Unmarshal(fieldsJSON, &record.Fields); err != nil {
		return GroundRecord{}, fmt.Errorf("decode fields for %s: %w", record.Envelope.RecordID, err)
	}
	return record, nil
}

func (s *PostgresStore) Close() error {
	if s == nil || s.db == nil {
		return nil
	}
	return s.db.Close()
}

func nullIfEmpty(value string) any {
	if value == "" {
		return nil
	}
	return value
}
