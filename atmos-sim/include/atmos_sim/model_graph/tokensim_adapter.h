#pragma once

#include "atmos_sim/model_graph/request_generator.h"
#include "atmos_sim/sim_core/sim_time.h"
#include <cstdint>
#include <string>
#include <vector>

namespace atmos_sim {

/// TokenSim event types consumed at the serving/workload boundary.
enum class TokenSimEventType : uint8_t {
    REQUEST_ARRIVED,
    PREFILL_STARTED,
    DECODE_TOKEN_READY,
    REQUEST_COMPLETED,
};

/// Minimal TokenSim trace event representation for workload import.
struct TokenSimEvent {
    uint64_t request_id = 0;
    SimTime timestamp = 0;
    TokenSimEventType type = TokenSimEventType::REQUEST_ARRIVED;
    uint32_t prompt_tokens = 0;
    uint32_t output_tokens = 0;
    uint32_t priority = 0;
    std::string model_name;
};

/// Defaults applied when TokenSim traces omit request-shape fields.
struct TokenSimAdapterConfig {
    RequestShape default_shape;
};

/// Converts TokenSim serving traces into ATMOS simulator timed requests.
class TokenSimAdapter {
public:
    explicit TokenSimAdapter(TokenSimAdapterConfig config = {});

    /// Import request arrivals from TokenSim events, sorted deterministically.
    std::vector<TimedRequest> to_workload(const std::vector<TokenSimEvent>& events) const;

    const TokenSimAdapterConfig& config() const { return config_; }

private:
    TokenSimAdapterConfig config_;
};

} // namespace atmos_sim