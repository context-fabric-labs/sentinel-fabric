#pragma once

#include "atmos_sim/sim_core/sim_time.h"
#include <cstdint>
#include <vector>
#include <string>
#include <random>

namespace atmos_sim {

/// Request shape for workload generation.
struct RequestShape {
    uint32_t prompt_tokens = 1024;
    uint32_t max_output_tokens = 256;
    uint32_t priority = 0;
    std::string model_name;
};

/// A timed request in the workload.
struct TimedRequest {
    SimTime arrival_time = 0;
    RequestShape shape;
    uint64_t request_id = 0;
};

/// Arrival pattern types.
enum class ArrivalPattern : uint8_t {
    POISSON,
    UNIFORM,
    BURST,
    FIXED_INTERVAL,
};

/// Configuration for the request generator.
struct RequestGeneratorConfig {
    ArrivalPattern pattern = ArrivalPattern::POISSON;
    double arrival_rate_rps = 10.0;  // Requests per second (for Poisson)
    SimTime interval_ns = ms_to_sim(100);  // For fixed interval
    uint64_t total_requests = 1000;
    uint32_t seed = 42;  // Deterministic seed
    RequestShape default_shape;
    // Distribution parameters for prompt/output lengths
    uint32_t prompt_tokens_min = 128;
    uint32_t prompt_tokens_max = 4096;
    uint32_t output_tokens_min = 64;
    uint32_t output_tokens_max = 1024;
};

/// Generates a deterministic sequence of timed requests.
class RequestGenerator {
public:
    explicit RequestGenerator(const RequestGeneratorConfig& config);

    /// Generate the full workload.
    std::vector<TimedRequest> generate();

    /// Generate a single request at the given index (for lazy generation).
    TimedRequest generate_request(uint64_t index);

    const RequestGeneratorConfig& config() const { return config_; }

private:
    RequestGeneratorConfig config_;
    std::mt19937 rng_;

    SimTime next_arrival_time(uint64_t index);
    uint32_t random_prompt_tokens();
    uint32_t random_output_tokens();
};

} // namespace atmos_sim
