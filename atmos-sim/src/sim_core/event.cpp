#include "atmos_sim/sim_core/event.h"

namespace atmos_sim {

const char* event_type_name(EventType type) {
    switch (type) {
        case EventType::RESOURCE_ACQUIRED:      return "RESOURCE_ACQUIRED";
        case EventType::RESOURCE_RELEASED:      return "RESOURCE_RELEASED";
        case EventType::QUEUE_ENQUEUED:         return "QUEUE_ENQUEUED";
        case EventType::QUEUE_DEQUEUED:         return "QUEUE_DEQUEUED";
        case EventType::TRANSFER_STARTED:       return "TRANSFER_STARTED";
        case EventType::TRANSFER_COMPLETED:     return "TRANSFER_COMPLETED";
        case EventType::COMPUTE_STARTED:        return "COMPUTE_STARTED";
        case EventType::COMPUTE_COMPLETED:      return "COMPUTE_COMPLETED";
        case EventType::DEPENDENCY_SATISFIED:   return "DEPENDENCY_SATISFIED";
        case EventType::MEMORY_READ_STARTED:    return "MEMORY_READ_STARTED";
        case EventType::MEMORY_READ_COMPLETED:  return "MEMORY_READ_COMPLETED";
        case EventType::MEMORY_WRITE_STARTED:   return "MEMORY_WRITE_STARTED";
        case EventType::MEMORY_WRITE_COMPLETED: return "MEMORY_WRITE_COMPLETED";
        case EventType::DMA_STARTED:            return "DMA_STARTED";
        case EventType::DMA_COMPLETED:          return "DMA_COMPLETED";
        case EventType::PCIE_TRANSFER_STARTED:  return "PCIE_TRANSFER_STARTED";
        case EventType::PCIE_TRANSFER_COMPLETED:return "PCIE_TRANSFER_COMPLETED";
        case EventType::REQUEST_ARRIVED:        return "REQUEST_ARRIVED";
        case EventType::REQUEST_COMPLETED:      return "REQUEST_COMPLETED";
        case EventType::SCHEDULER_DISPATCHED:   return "SCHEDULER_DISPATCHED";
        case EventType::CUSTOM:                 return "CUSTOM";
    }
    return "UNKNOWN";
}

bool Event::operator<(const Event& other) const {
    if (timestamp != other.timestamp) return timestamp < other.timestamp;
    return sequence < other.sequence;
}

bool Event::operator>(const Event& other) const {
    if (timestamp != other.timestamp) return timestamp > other.timestamp;
    return sequence > other.sequence;
}

bool Event::operator==(const Event& other) const {
    return timestamp == other.timestamp && sequence == other.sequence;
}

} // namespace atmos_sim
