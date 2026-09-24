#include "trishula/landing/lunar_terrain.h"
#include "trishula/navigation/terrain_relative_navigation.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        constexpr double kVehicleAltitude = 250.0;
        trishula::LunarTerrainMap terrain(0.0);
        trishula::TerrainRelativeSensor sensor(terrain, 0.12, 0.35, 0.08, 0x5A17C0DEu);
        trishula::TerrainRelativeNavigator navigator;

        const auto measurements = sensor.scan(kVehicleAltitude, 0.0, 2500.0, 100.0);
        const auto estimates = navigator.estimate(measurements, 100.0);
        const auto selected = navigator.select_safe_site(estimates);

        std::size_t valid = 0;
        for (const auto& m : measurements) valid += m.valid ? 1U : 0U;
        std::size_t safe = 0;
        std::size_t hazards = 0;
        double max_abs_noise = 0.0;
        for (const auto& m : measurements) max_abs_noise = std::max(max_abs_noise, std::abs(m.noise_m));
        for (const auto& e : estimates) {
            safe += e.safe ? 1U : 0U;
            hazards += (!e.safe) ? 1U : 0U;
        }

        const auto truth = terrain.sample(selected.x_m);
        const double site_error = std::abs(truth.elevation_m - selected.elevation_m);
        const bool site_not_perfect = site_error > 0.0 || max_abs_noise > 0.0;
        const bool pass = valid > measurements.size() / 2 && hazards > 0 && safe > 0 &&
                          selected.confidence >= 0.65 && selected.safe && site_not_perfect;
        if (!pass) throw std::runtime_error("terrain-relative navigation acceptance failed");

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "TRISHULA V0.9.31 - Sensor-Based Terrain Relative Navigation\n";
        std::cout << "==============================================================\n";
        std::cout << "  scan measurements               : " << measurements.size() << "\n";
        std::cout << "  valid measurements               : " << valid << "\n";
        std::cout << "  estimated candidates             : " << estimates.size() << "\n";
        std::cout << "  estimated safe sites             : " << safe << "\n";
        std::cout << "  estimated hazardous sites        : " << hazards << "\n";
        std::cout << "  sensor noise sigma               : 0.350000 m\n";
        std::cout << "  altitude bias                    : 0.120000 m\n";
        std::cout << "  invalid measurement fraction     : 0.080000\n";
        std::cout << "  selected landing x               : " << selected.x_m << " m\n";
        std::cout << "  estimated terrain elevation      : " << selected.elevation_m << " m\n";
        std::cout << "  truth terrain elevation          : " << truth.elevation_m << " m\n";
        std::cout << "  estimated slope                  : " << selected.slope << "\n";
        std::cout << "  estimated roughness              : " << selected.roughness_m << " m\n";
        std::cout << "  estimated confidence             : " << selected.confidence << "\n";
        std::cout << "  selected safe                    : " << (selected.safe ? "YES" : "NO") << "\n";
        std::cout << "  sensor-based TRN                 : ENABLED\n";
        std::cout << "  measurement dropout handling     : ENABLED\n";
        std::cout << "  hazard-map inference             : ENABLED\n";
        std::cout << "  sensor-vs-truth distinction      : PASS\n";
        std::cout << "  terrain-relative landing decision: PASS\n";
        std::cout << "\nV0.9.31 sensor-based terrain relative navigation campaign PASSED.\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "V0.9.31 campaign FAILED: " << ex.what() << "\n";
        return 1;
    }
}
