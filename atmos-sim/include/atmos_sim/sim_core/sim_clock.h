#pragma once

#include "atmos_sim/sim_core/sim_time.h"

namespace atmos_sim {

/// Virtual simulation clock — advances only via processed events, never wall-clock.
class SimClock {
public:
    SimClock();

    /// Current simulation time.
    SimTime now() const;

    /// Advance clock to the given time (must be >= current time).
    void advance_to(SimTime time);

    /// Reset clock to zero.
    void reset();

private:
    SimTime current_time_;
};

} // namespace atmos_sim
