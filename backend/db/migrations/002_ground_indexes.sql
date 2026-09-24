-- Additional operational index for source/kind filtering.
CREATE INDEX IF NOT EXISTS idx_ground_records_source_kind_time
    ON ground_records (source_node, kind, mission_timestamp_ns DESC);
