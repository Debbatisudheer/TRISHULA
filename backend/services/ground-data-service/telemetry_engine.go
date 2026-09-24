package main

import (
	"fmt"
	"math"
	"strconv"
	"strings"
)

type TelemetryQualityClass string

const (
	TelemetryQualityGood     TelemetryQualityClass = "GOOD"
	TelemetryQualityDegraded TelemetryQualityClass = "DEGRADED"
	TelemetryQualityBad      TelemetryQualityClass = "BAD"
)

type TelemetryLimitStatus string

const (
	TelemetryLimitNormal     TelemetryLimitStatus = "NORMAL"
	TelemetryLimitLow        TelemetryLimitStatus = "LOW"
	TelemetryLimitHigh       TelemetryLimitStatus = "HIGH"
	TelemetryLimitOutOfRange TelemetryLimitStatus = "OUT_OF_RANGE"
)

type telemetryProfile struct {
	canonicalUnit string
	min           *float64
	max           *float64
}

func ptrFloat(v float64) *float64 { return &v }

var telemetryProfiles = map[string]telemetryProfile{
	"battery":     {canonicalUnit: "%", min: ptrFloat(0), max: ptrFloat(100)},
	"temperature": {canonicalUnit: "K", min: ptrFloat(150), max: ptrFloat(450)},
	"voltage":     {canonicalUnit: "V", min: ptrFloat(0), max: ptrFloat(60)},
	"current":     {canonicalUnit: "A", min: ptrFloat(0), max: ptrFloat(100)},
	"speed":       {canonicalUnit: "m/s", min: ptrFloat(0), max: ptrFloat(50)},
}

type telemetryEvaluation struct {
	metric         string
	value          float64
	unit           string
	qualityClass   TelemetryQualityClass
	limitStatus    TelemetryLimitStatus
	operational    string
	normalizedText string
}

func classifyTelemetryQuality(q float64) TelemetryQualityClass {
	switch {
	case q >= 0.95:
		return TelemetryQualityGood
	case q >= 0.80:
		return TelemetryQualityDegraded
	default:
		return TelemetryQualityBad
	}
}

func normalizeTelemetryValue(metric, rawValue, rawUnit string) (float64, string, error) {
	value, err := strconv.ParseFloat(strings.TrimSpace(rawValue), 64)
	if err != nil || math.IsNaN(value) || math.IsInf(value, 0) {
		return 0, "", fmt.Errorf("telemetry value must be a finite number")
	}

	unit := strings.TrimSpace(rawUnit)
	if unit == "" {
		if profile, ok := telemetryProfiles[metric]; ok {
			unit = profile.canonicalUnit
		} else {
			unit = "unknown"
		}
	}

	switch metric {
	case "battery":
		switch strings.ToLower(unit) {
		case "%", "percent", "pct":
			return value, "%", nil
		default:
			return 0, "", fmt.Errorf("unsupported battery unit %q", rawUnit)
		}
	case "temperature":
		switch strings.ToLower(unit) {
		case "k", "kelvin":
			return value, "K", nil
		case "c", "celsius", "°c":
			return value + 273.15, "K", nil
		case "f", "fahrenheit", "°f":
			return (value-32)*5/9 + 273.15, "K", nil
		default:
			return 0, "", fmt.Errorf("unsupported temperature unit %q", rawUnit)
		}
	case "voltage":
		if strings.EqualFold(unit, "v") || strings.EqualFold(unit, "volt") || strings.EqualFold(unit, "volts") {
			return value, "V", nil
		}
		return 0, "", fmt.Errorf("unsupported voltage unit %q", rawUnit)
	case "current":
		if strings.EqualFold(unit, "a") || strings.EqualFold(unit, "amp") || strings.EqualFold(unit, "amps") {
			return value, "A", nil
		}
		return 0, "", fmt.Errorf("unsupported current unit %q", rawUnit)
	case "speed":
		if strings.EqualFold(unit, "m/s") || strings.EqualFold(unit, "mps") {
			return value, "m/s", nil
		}
		return 0, "", fmt.Errorf("unsupported speed unit %q", rawUnit)
	default:
		return value, unit, nil
	}
}

func evaluateTelemetryLimits(metric string, value float64) (TelemetryLimitStatus, error) {
	profile, ok := telemetryProfiles[metric]
	if !ok || profile.min == nil || profile.max == nil {
		return TelemetryLimitNormal, nil
	}
	if value < *profile.min || value > *profile.max {
		return TelemetryLimitOutOfRange, fmt.Errorf("%s value %.6f is outside [%g,%g]", metric, value, *profile.min, *profile.max)
	}
	switch metric {
	case "battery":
		if value < 30 {
			return TelemetryLimitLow, nil
		}
	case "temperature":
		if value < 250 {
			return TelemetryLimitLow, nil
		}
		if value > 330 {
			return TelemetryLimitHigh, nil
		}
	}
	return TelemetryLimitNormal, nil
}

func deriveTelemetryOperationalState(metric string, value float64) string {
	switch metric {
	case "battery":
		switch {
		case value < 20:
			return "CRITICAL"
		case value < 30:
			return "LOW_POWER"
		default:
			return "NOMINAL"
		}
	case "temperature":
		switch {
		case value < 250:
			return "COLD"
		case value > 330:
			return "HOT"
		default:
			return "NOMINAL"
		}
	default:
		return "UNKNOWN"
	}
}

func evaluateTelemetry(record GroundRecord) (telemetryEvaluation, error) {
	if err := processTelemetryRecordBasic(record); err != nil {
		return telemetryEvaluation{}, err
	}
	metric := strings.ToLower(strings.TrimSpace(record.Fields["metric"]))
	if metric == "" {
		metric = "multi-field"
	}
	rawValue := strings.TrimSpace(record.Fields["value"])
	if rawValue == "" {
		for key, candidate := range record.Fields {
			if key != "metric" && key != "unit" {
				rawValue = strings.TrimSpace(candidate)
				break
			}
		}
	}
	if rawValue == "" {
		return telemetryEvaluation{}, fmt.Errorf("telemetry value is required")
	}
	value, unit, err := normalizeTelemetryValue(metric, rawValue, record.Fields["unit"])
	if err != nil {
		return telemetryEvaluation{}, err
	}
	qualityClass := classifyTelemetryQuality(record.Envelope.Quality)
	limitStatus, limitErr := evaluateTelemetryLimits(metric, value)
	if limitErr != nil {
		return telemetryEvaluation{}, limitErr
	}
	return telemetryEvaluation{
		metric:         metric,
		value:          value,
		unit:           unit,
		qualityClass:   qualityClass,
		limitStatus:    limitStatus,
		operational:    deriveTelemetryOperationalState(metric, value),
		normalizedText: strconv.FormatFloat(value, 'f', -1, 64),
	}, nil
}

func processTelemetryRecordBasic(record GroundRecord) error {
	if record.Envelope.Kind != KindTelemetry {
		return fmt.Errorf("telemetry handler received %q", record.Envelope.Kind)
	}
	if len(record.Fields) == 0 {
		return fmt.Errorf("telemetry payload is empty")
	}
	return nil
}

func processTelemetryEngineOutput(record GroundRecord) (ProcessingResult, error) {
	eval, err := evaluateTelemetry(record)
	if err != nil {
		return ProcessingResult{}, err
	}
	operation := "telemetry-evaluate:" + eval.metric
	attrs := map[string]string{
		"metric":            eval.metric,
		"value":             eval.normalizedText,
		"unit":              eval.unit,
		"quality":           fmt.Sprintf("%.6f", record.Envelope.Quality),
		"quality_class":     string(eval.qualityClass),
		"limit_status":      string(eval.limitStatus),
		"operational_state": eval.operational,
	}
	if rawUnit := strings.TrimSpace(record.Fields["unit"]); rawUnit != "" {
		attrs["input_unit"] = rawUnit
	}
	return buildProcessingResult(record, operation, attrs), nil
}
