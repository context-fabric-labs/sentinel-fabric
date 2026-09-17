#include "atmos_sim/sim_core/sim_time.h"
#include <atomic>

namespace atmos_sim {

const char* compute_class_name(ComputeClass cls) {
    switch (cls) {
        case ComputeClass::GEMM:        return "GEMM";
        case ComputeClass::ATTENTION:   return "ATTENTION";
        case ComputeClass::ELEMENTWISE: return "ELEMENTWISE";
        case ComputeClass::REDUCE:      return "REDUCE";
        case ComputeClass::KV_READ:     return "KV_READ";
        case ComputeClass::KV_WRITE:    return "KV_WRITE";
        case ComputeClass::OTHER:       return "OTHER";
    }
    return "UNKNOWN";
}

static std::atomic<uint64_t> g_event_sequence{0};

uint64_t next_event_sequence() {
    return g_event_sequence.fetch_add(1, std::memory_order_relaxed);
}

void reset_event_sequence() {
    g_event_sequence.store(0, std::memory_order_relaxed);
}

} // namespace atmos_sim
