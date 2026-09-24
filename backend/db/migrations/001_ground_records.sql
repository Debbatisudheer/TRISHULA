-- TRISHULA V0.9.59
-- PostgreSQL ground-record schema.
-- Append-only mission data plus indexes for operational queries.

CREATE TABLE IF NOT EXISTS ground_records (
    record_id             TEXT NOT NULL,
    schema_version        INTEGER NOT NULL,
    kind                   TEXT NOT NULL,
    mission_id             TEXT NOT NULL,
    source_node            TEXT NOT NULL,
    origin_node            TEXT NOT NULL,
    destination_node       TEXT NOT NULL,
    priority               TEXT NOT NULL,
    application_id        INTEGER NOT NULL,
    mission_timestamp_ns  BIGINT NOT NULL,
    sequence_number       BIGINT NOT NULL,
    quality                DOUBLE PRECISION NOT NULL,
    correlation_id         TEXT,
    payload_schema         TEXT NOT NULL,
    fields_json            JSONB NOT NULL,
    ingested_at            TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (record_id)
);

CREATE INDEX IF NOT EXISTS idx_ground_records_source_sequence
    ON ground_records (source_node, sequence_number DESC);

CREATE INDEX IF NOT EXISTS idx_ground_records_kind_time
    ON ground_records (kind, mission_timestamp_ns DESC);

CREATE INDEX IF NOT EXISTS idx_ground_records_mission_time
    ON ground_records (mission_id, mission_timestamp_ns DESC);

CREATE INDEX IF NOT EXISTS idx_ground_records_correlation
    ON ground_records (correlation_id)
    WHERE correlation_id IS NOT NULL;
