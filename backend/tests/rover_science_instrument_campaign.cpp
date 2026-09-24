#include <cmath>
#include <iostream>

#include "trishula/rover/science_instruments.h"

namespace {

double l1_error(const trishula::LunarMaterialComposition& a,
                const trishula::LunarMaterialComposition& b) {
    return std::abs(a.si - b.si) + std::abs(a.fe - b.fe) + std::abs(a.al - b.al) +
           std::abs(a.ca - b.ca) + std::abs(a.mg - b.mg) + std::abs(a.ti - b.ti);
}

} // namespace

int main() {
    using namespace trishula;

    LunarMaterialModel material;
    RoverScienceInstrumentSuite instruments(material);

    const auto observation = instruments.observe(7U, -700.0);
    const double libs_error = l1_error(observation.truth_composition, observation.libs.inferred_abundance);
    const double apxs_error = l1_error(observation.truth_composition, observation.apxs.inferred_abundance);

    if (!observation.instrument_suite_healthy || !observation.observation_valid) return 1;
    if (!observation.libs.acquired || !observation.libs.laser_fired) return 2;
    if (observation.libs.laser_energy_mj <= 0.0 || observation.libs.plasma_temperature_k <= 0.0) return 3;
    if (observation.libs.signal_to_noise < 20.0 || libs_error > 1.0e-12) return 4;
    if (!observation.apxs.acquired || !observation.apxs.excitation_active) return 5;
    if (observation.apxs.exposure_s <= 0.0 || observation.apxs.counts_per_second <= 0.0) return 6;
    if (apxs_error > 1.0e-12) return 7;
    if (!observation.camera.acquired || !observation.camera.illuminated) return 8;

    const auto partial = instruments.observe(8U, 1250.0, false, true, true);
    if (partial.instrument_suite_healthy || partial.observation_valid) return 9;
    if (partial.libs.acquired) return 10;
    if (!partial.apxs.acquired || !partial.camera.acquired) return 11;

    const auto repeat = instruments.observe(7U, -700.0);
    if (std::abs(repeat.libs.inferred_abundance.si - observation.libs.inferred_abundance.si) > 1.0e-12) return 12;
    if (std::abs(repeat.apxs.inferred_abundance.fe - observation.apxs.inferred_abundance.fe) > 1.0e-12) return 13;

    std::cout << "TRISHULA V0.9.46 - Rover Physical Science Instruments\n"
              << "==============================================================\n"
              << "  target id                         : 7\n"
              << "  surface x                         : -700.000000 m\n"
              << "  LIBS laser fired                  : YES\n"
              << "  LIBS plasma temperature           : " << observation.libs.plasma_temperature_k << " K\n"
              << "  LIBS signal-to-noise              : " << observation.libs.signal_to_noise << "\n"
              << "  LIBS abundance reconstruction     : PASS\n"
              << "  APXS excitation                   : YES\n"
              << "  APXS exposure                     : " << observation.apxs.exposure_s << " s\n"
              << "  APXS count rate                   : " << observation.apxs.counts_per_second << " cps\n"
              << "  APXS abundance reconstruction     : PASS\n"
              << "  surface camera observation        : PASS\n"
              << "  instrument health fault injection : PASS\n"
              << "  raw-signal -> calibrated result   : PASS\n"
              << "  physical-observation simulation   : PASS\n"
              << "  observation quality               : " << observation.quality << "\n"
              << "\nV0.9.46 rover physical science instrument campaign PASSED.\n";
    return 0;
}
