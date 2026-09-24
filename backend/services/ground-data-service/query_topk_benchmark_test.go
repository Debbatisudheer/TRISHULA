package main

import (
	"testing"
)

func benchmarkQueryRecords(n int) []GroundRecord {
	records := make([]GroundRecord, n)
	for i := range records {
		seq := uint64(i + 1)
		records[i] = queryTopKRecord("REC-"+itoaBenchmark(i), seq*1000, seq)
	}
	return records
}

func itoaBenchmark(v int) string {
	const digits = "0123456789"
	if v == 0 {
		return "0"
	}
	buf := [20]byte{}
	pos := len(buf)
	for v > 0 {
		pos--
		buf[pos] = digits[v%10]
		v /= 10
	}
	return string(buf[pos:])
}

func applyQuerySortBaseline(records []GroundRecord, query RecordQuery) []GroundRecord {
	out := make([]GroundRecord, 0, len(records))
	for _, record := range records {
		if recordMatches(record, query) {
			out = append(out, record)
		}
	}
	sortQueryResults(out)
	if query.Limit > 0 && len(out) > query.Limit {
		out = out[:query.Limit]
	}
	return out
}

func BenchmarkRecordQueryTopKSelection(b *testing.B) {
	records := benchmarkQueryRecords(100000)
	query := RecordQuery{MissionID: "TRISHULA", Limit: 50}
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, _ = applyQuery(records, query)
	}
}

func BenchmarkRecordQueryFullSortSelection(b *testing.B) {
	records := benchmarkQueryRecords(100000)
	query := RecordQuery{MissionID: "TRISHULA", Limit: 50}
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = applyQuerySortBaseline(records, query)
	}
}
