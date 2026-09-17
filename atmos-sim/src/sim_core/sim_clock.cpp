#include "atmos_sim/sim_core/sim_clock.h"
#include <stdexcept>

namespace atmos_sim {

SimClock::SimClock() : current_time_(SIM_TIME_ZERO) {}

SimTime SimClock::now() const {
    return current_time_;
}

void SimClock::advance_to(SimTime time) {
    if (time < current_time_) {
        throw std::runtime_error("SimClock: cannot advance backwards");
    }
    current_time_ = time;
}

void SimClock::reset() {
    current_time_ = SIM_TIME_ZERO;
}

} // namespace atmos_sim
