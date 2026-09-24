package main

import (
	"fmt"
	"math"
	"strconv"
	"strings"
)

type ScienceQualityClass string

const (
	ScienceQualityGood     ScienceQualityClass = "GOOD"
	ScienceQualityDegraded ScienceQualityClass = "DEGRADED"
	ScienceQualityBad      ScienceQualityClass = "BAD"
)

type ScienceEvaluation struct {
	Instrument          string
	Target              string
	Measurement         string
	QualityClass        ScienceQualityClass
	ScientificUsability string
	LIBSSignalToNoise   *float64
	APXSCountsPerSecond *float64
	APXSExposureSeconds *float64
	CameraIlluminated   *bool
	LIBSAPXSAgreement   *float64
	FusedSi             *float64
	FusedFe             *float64
	FusedAl             *float64
	FusedCa             *float64
	FusedMg             *float64
	FusedTi             *float64
	DerivedFeMgRatio    *float64
	DerivedTiSiRatio    *float64
	Interpretation      string
}

func classifyScienceQuality(q float64) ScienceQualityClass {
	switch {
	case q >= 0.90:
		return ScienceQualityGood
	case q >= 0.70:
		return ScienceQualityDegraded
	default:
		return ScienceQualityBad
	}
}

func parseOptionalFinite(fields map[string]string, key string) (*float64, error) {
	raw := strings.TrimSpace(fields[key])
	if raw == "" {
		return nil, nil
	}
	value, err := strconv.ParseFloat(raw, 64)
	if err != nil || math.IsNaN(value) || math.IsInf(value, 0) {
		return nil, fmt.Errorf("science field %q must be a finite number", key)
	}
	return &value, nil
}

func parseOptionalBool(fields map[string]string, key string) (*bool, error) {
	raw := strings.TrimSpace(fields[key])
	if raw == "" {
		return nil, nil
	}
	value, err := strconv.ParseBool(raw)
	if err != nil {
		return nil, fmt.Errorf("science field %q must be true or false", key)
	}
	return &value, nil
}

func parseComposition(fields map[string]string, prefix string) ([6]float64, bool, error) {
	keys := [...]string{"si", "fe", "al", "ca", "mg", "ti"}
	var values [6]float64
	present := false
	for i, element := range keys {
		raw := strings.TrimSpace(fields[prefix+"_"+element])
		if raw == "" {
			continue
		}
		present = true
		value, err := strconv.ParseFloat(raw, 64)
		if err != nil || math.IsNaN(value) || math.IsInf(value, 0) || value < 0 {
			return [6]float64{}, false, fmt.Errorf("science composition field %q must be a finite non-negative number", prefix+"_"+element)
		}
		values[i] = value
	}
	if !present {
		return [6]float64{}, false, nil
	}
	for i, element := range keys {
		if strings.TrimSpace(fields[prefix+"_"+element]) == "" {
			return [6]float64{}, false, fmt.Errorf("science composition for %s is incomplete; missing %s", prefix, element)
		}
		_ = element
		_ = i
	}
	return values, true, nil
}

func normalizeComposition(values [6]float64) ([6]float64, error) {
	sum := 0.0
	for _, value := range values {
		sum += value
	}
	if sum <= 0 {
		return [6]float64{}, fmt.Errorf("science composition sum must be positive")
	}
	for i := range values {
		values[i] /= sum
	}
	return values, nil
}

func rmsDifference(a, b [6]float64) float64 {
	squared := 0.0
	for i := range a {
		d := a[i] - b[i]
		squared += d * d
	}
	return math.Sqrt(squared / 6.0)
}

func evaluateScience(record GroundRecord) (ScienceEvaluation, error) {
	if _, err := processScienceRecord(record); err != nil {
		return ScienceEvaluation{}, err
	}

	instrument := strings.ToUpper(strings.TrimSpace(record.Fields["instrument"]))
	target := strings.TrimSpace(record.Fields["target"])
	measurement := strings.TrimSpace(record.Fields["measurement"])
	if measurement == "" {
		measurement = "science_observation"
	}
	if instrument == "" || target == "" {
		return ScienceEvaluation{}, fmt.Errorf("science instrument and target are required")
	}

	evaluation := ScienceEvaluation{
		Instrument:          instrument,
		Target:              target,
		Measurement:         measurement,
		QualityClass:        classifyScienceQuality(record.Envelope.Quality),
		ScientificUsability: "NOT_USABLE",
	}

	var err error
	evaluation.LIBSSignalToNoise, err = parseOptionalFinite(record.Fields, "libs_signal_to_noise")
	if err != nil {
		return ScienceEvaluation{}, err
	}
	evaluation.APXSCountsPerSecond, err = parseOptionalFinite(record.Fields, "apxs_counts_per_second")
	if err != nil {
		return ScienceEvaluation{}, err
	}
	evaluation.APXSExposureSeconds, err = parseOptionalFinite(record.Fields, "apxs_exposure_s")
	if err != nil {
		return ScienceEvaluation{}, err
	}
	evaluation.CameraIlluminated, err = parseOptionalBool(record.Fields, "camera_illuminated")
	if err != nil {
		return ScienceEvaluation{}, err
	}

	libs, libsPresent, err := parseComposition(record.Fields, "libs")
	if err != nil {
		return ScienceEvaluation{}, err
	}
	apxs, apxsPresent, err := parseComposition(record.Fields, "apxs")
	if err != nil {
		return ScienceEvaluation{}, err
	}

	usable := record.Envelope.Quality >= 0.70
	instrumentEvidence := false
	if evaluation.LIBSSignalToNoise != nil && *evaluation.LIBSSignalToNoise > 0 {
		instrumentEvidence = true
	}
	if evaluation.APXSCountsPerSecond != nil && *evaluation.APXSCountsPerSecond > 0 && evaluation.APXSExposureSeconds != nil && *evaluation.APXSExposureSeconds > 0 {
		instrumentEvidence = true
	}
	if evaluation.CameraIlluminated != nil && *evaluation.CameraIlluminated {
		instrumentEvidence = true
	}

	if libsPresent && apxsPresent {
		libs, err = normalizeComposition(libs)
		if err != nil {
			return ScienceEvaluation{}, err
		}
		apxs, err = normalizeComposition(apxs)
		if err != nil {
			return ScienceEvaluation{}, err
		}
		agreement := rmsDifference(libs, apxs)
		evaluation.LIBSAPXSAgreement = &agreement
		var fused [6]float64
		for i := range fused {
			fused[i] = 0.5 * (libs[i] + apxs[i])
		}
		fused, err = normalizeComposition(fused)
		if err != nil {
			return ScienceEvaluation{}, err
		}
		evaluation.FusedSi = &fused[0]
		evaluation.FusedFe = &fused[1]
		evaluation.FusedAl = &fused[2]
		evaluation.FusedCa = &fused[3]
		evaluation.FusedMg = &fused[4]
		evaluation.FusedTi = &fused[5]
		feMg := fused[1] / math.Max(fused[4], 1e-12)
		tiSi := fused[5] / math.Max(fused[0], 1e-12)
		evaluation.DerivedFeMgRatio = &feMg
		evaluation.DerivedTiSiRatio = &tiSi

		agreementOK := agreement <= 1e-6
		instrumentEvidence = instrumentEvidence || (libsPresent && apxsPresent)
		usable = usable && agreementOK && instrumentEvidence
		switch {
		case tiSi > 0.14 && fused[1] > 0.18:
			evaluation.Interpretation = "Measured composition is relatively Ti/Fe enriched within the simulator's analysis thresholds."
		case fused[0] > 0.34 && fused[2] > 0.14:
			evaluation.Interpretation = "Measured composition is relatively Si/Al enriched within the simulator's analysis thresholds."
		default:
			evaluation.Interpretation = "Measured composition is within the baseline simulator classification range."
		}
	} else {
		// Single-instrument or metadata-only science records remain useful as
		// validated observations, but they are not claimed to be a fused science product.
		if instrument == "LIBS" && evaluation.LIBSSignalToNoise != nil && *evaluation.LIBSSignalToNoise > 0 {
			instrumentEvidence = true
		}
		if instrument == "APXS" && evaluation.APXSCountsPerSecond != nil && evaluation.APXSExposureSeconds != nil && *evaluation.APXSCountsPerSecond > 0 && *evaluation.APXSExposureSeconds > 0 {
			instrumentEvidence = true
		}
		if instrumentEvidence && usable {
			evaluation.ScientificUsability = "USABLE_SINGLE_INSTRUMENT"
		} else if usable {
			evaluation.ScientificUsability = "VALIDATED_OBSERVATION"
		}
	}

	if usable && evaluation.ScientificUsability == "NOT_USABLE" {
		evaluation.ScientificUsability = "USABLE"
	}
	if !usable {
		evaluation.ScientificUsability = "NOT_USABLE"
	}
	return evaluation, nil
}

func processScienceEngineOutput(record GroundRecord) (ProcessingResult, error) {
	evaluation, err := evaluateScience(record)
	if err != nil {
		return ProcessingResult{}, err
	}
	attributes := map[string]string{
		"instrument":           evaluation.Instrument,
		"target":               evaluation.Target,
		"measurement":          evaluation.Measurement,
		"quality":              fmt.Sprintf("%.6f", record.Envelope.Quality),
		"quality_class":        string(evaluation.QualityClass),
		"scientific_usability": evaluation.ScientificUsability,
	}
	if evaluation.LIBSSignalToNoise != nil {
		attributes["libs_signal_to_noise"] = strconv.FormatFloat(*evaluation.LIBSSignalToNoise, 'f', -1, 64)
	}
	if evaluation.APXSCountsPerSecond != nil {
		attributes["apxs_counts_per_second"] = strconv.FormatFloat(*evaluation.APXSCountsPerSecond, 'f', -1, 64)
	}
	if evaluation.APXSExposureSeconds != nil {
		attributes["apxs_exposure_s"] = strconv.FormatFloat(*evaluation.APXSExposureSeconds, 'f', -1, 64)
	}
	if evaluation.CameraIlluminated != nil {
		attributes["camera_illuminated"] = strconv.FormatBool(*evaluation.CameraIlluminated)
	}
	if evaluation.LIBSAPXSAgreement != nil {
		attributes["libs_apxs_abundance_agreement"] = strconv.FormatFloat(*evaluation.LIBSAPXSAgreement, 'f', 12, 64)
	}
	if evaluation.FusedSi != nil {
		attributes["fused_si"] = strconv.FormatFloat(*evaluation.FusedSi, 'f', 9, 64)
		attributes["fused_fe"] = strconv.FormatFloat(*evaluation.FusedFe, 'f', 9, 64)
		attributes["fused_al"] = strconv.FormatFloat(*evaluation.FusedAl, 'f', 9, 64)
		attributes["fused_ca"] = strconv.FormatFloat(*evaluation.FusedCa, 'f', 9, 64)
		attributes["fused_mg"] = strconv.FormatFloat(*evaluation.FusedMg, 'f', 9, 64)
		attributes["fused_ti"] = strconv.FormatFloat(*evaluation.FusedTi, 'f', 9, 64)
		attributes["derived_fe_mg_ratio"] = strconv.FormatFloat(*evaluation.DerivedFeMgRatio, 'f', 9, 64)
		attributes["derived_ti_si_ratio"] = strconv.FormatFloat(*evaluation.DerivedTiSiRatio, 'f', 9, 64)
		attributes["interpretation"] = evaluation.Interpretation
	}
	operation := "science-evaluate:" + evaluation.Instrument + ":" + evaluation.Target
	return buildProcessingResult(record, operation, attributes), nil
}
