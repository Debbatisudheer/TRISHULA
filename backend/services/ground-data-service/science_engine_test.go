package main

import (
	"math"
	"testing"
)

func TestScienceEngineSingleInstrumentLIBS(t *testing.T) {
	record := sampleRecord("SCI-LIBS-1", 20, KindScience)
	record.Envelope.Quality = 0.94
	record.Fields = map[string]string{
		"instrument":           "LIBS",
		"target":               "TARGET-7",
		"measurement":          "elemental_abundance",
		"libs_signal_to_noise": "42",
	}
	result, err := processScienceEngineOutput(record)
	if err != nil {
		t.Fatalf("unexpected science processing error: %v", err)
	}
	if result.Status != "accepted" || result.Attributes["scientific_usability"] != "USABLE_SINGLE_INSTRUMENT" {
		t.Fatalf("unexpected single-instrument result: %+v", result)
	}
	if result.Attributes["libs_signal_to_noise"] != "42" {
		t.Fatalf("unexpected LIBS SNR: %+v", result.Attributes)
	}
}

func TestScienceEngineQualityAndMetadataOnlyObservation(t *testing.T) {
	record := sampleRecord("SCI-META-1", 21, KindScience)
	record.Envelope.Quality = 0.72
	record.Fields = map[string]string{
		"instrument":  "CAMERA",
		"target":      "TARGET-8",
		"measurement": "context_image",
	}
	result, err := processScienceEngineOutput(record)
	if err != nil {
		t.Fatalf("unexpected metadata processing error: %v", err)
	}
	if result.Attributes["quality_class"] != "DEGRADED" {
		t.Fatalf("expected DEGRADED quality, got %+v", result.Attributes)
	}
	if result.Attributes["scientific_usability"] != "VALIDATED_OBSERVATION" {
		t.Fatalf("expected validated observation, got %+v", result.Attributes)
	}
}

func TestScienceEngineFusesLIBSAndAPXS(t *testing.T) {
	record := sampleRecord("SCI-FUSE-1", 22, KindScience)
	record.Envelope.Quality = 0.94
	record.Fields = map[string]string{
		"instrument":             "LIBS_APXS",
		"target":                 "TARGET-7",
		"measurement":            "elemental_abundance",
		"libs_signal_to_noise":   "42",
		"apxs_counts_per_second": "1850",
		"apxs_exposure_s":        "30",
		"camera_illuminated":     "true",
		"libs_si":                "0.40",
		"libs_fe":                "0.20",
		"libs_al":                "0.15",
		"libs_ca":                "0.10",
		"libs_mg":                "0.10",
		"libs_ti":                "0.05",
		"apxs_si":                "0.40",
		"apxs_fe":                "0.20",
		"apxs_al":                "0.15",
		"apxs_ca":                "0.10",
		"apxs_mg":                "0.10",
		"apxs_ti":                "0.05",
	}
	result, err := processScienceEngineOutput(record)
	if err != nil {
		t.Fatalf("unexpected fused science processing error: %v", err)
	}
	if result.Attributes["scientific_usability"] != "USABLE" {
		t.Fatalf("expected USABLE fused result, got %+v", result.Attributes)
	}
	agreement := result.Attributes["libs_apxs_abundance_agreement"]
	if agreement != "0.000000000000" {
		t.Fatalf("expected zero LIBS/APXS agreement error, got %q", agreement)
	}
	if result.Attributes["interpretation"] == "" {
		t.Fatal("expected scientific interpretation")
	}
	if result.ProcessorVersion != currentProcessorVersion {
		t.Fatalf("unexpected processor version: %s", result.ProcessorVersion)
	}
}

func TestScienceEngineRejectsMalformedComposition(t *testing.T) {
	record := sampleRecord("SCI-BAD-1", 23, KindScience)
	record.Fields = map[string]string{
		"instrument": "LIBS_APXS",
		"target":     "TARGET-7",
		"libs_si":    "0.4",
		"libs_fe":    "NaN",
	}
	if _, err := processScienceEngineOutput(record); err == nil {
		t.Fatal("expected malformed composition to fail")
	}
}

func TestScienceCompositionMathSanity(t *testing.T) {
	a := [6]float64{0.4, 0.2, 0.15, 0.1, 0.1, 0.05}
	b := a
	if got := rmsDifference(a, b); got != 0 {
		t.Fatalf("expected zero RMS difference, got %.12f", got)
	}
	for _, value := range a {
		if math.IsNaN(value) {
			t.Fatal("unexpected NaN")
		}
	}
}
