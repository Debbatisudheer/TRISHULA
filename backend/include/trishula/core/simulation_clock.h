#pragma once

namespace trishula {

class SimulationClock {
public:
    explicit SimulationClock(double time_step_seconds = 1.0);

    [[nodiscard]] double time() const noexcept;
    [[nodiscard]] double time_step() const noexcept;
    void advance();
    void advance(double delta_seconds);
    void reset(double start_time_seconds = 0.0);

private:
    double current_time_seconds_;
    double time_step_seconds_;
};

} // namespace trishula
