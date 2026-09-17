#include "atmos_sim/model_graph/request_generator.h"
#include <algorithm>

namespace atmos_sim {

RequestGenerator::RequestGenerator(const RequestGeneratorConfig& config)
    : config_(config), rng_(config.seed) {}

std::vector<TimedRequest> RequestGenerator::generate() {
    std::vector<TimedRequest> requests;
    requests.reserve(config_.total_requests);

    for (uint64_t i = 0; i < config_.total_requests; ++i) {
        requests.push_back(generate_request(i));
    }

    // Sort by arrival time
    std::sort(requests.begin(), requests.end(),
              [](const TimedRequest& a, const TimedRequest& b) {
                  return a.arrival_time < b.arrival_time;
              });

    return requests;
}

TimedRequest RequestGenerator::generate_request(uint64_t index) {
    TimedRequest req;
    req.request_id = index + 1;
    req.arrival_time = next_arrival_time(index);
    req.shape.prompt_tokens = random_prompt_tokens();
    req.shape.max_output_tokens = random_output_tokens();
    req.shape.model_name = config_.default_shape.model_name;
    req.shape.priority = config_.default_shape.priority;
    return req;
}

SimTime RequestGenerator::next_arrival_time(uint64_t index) {
    switch (config_.pattern) {
        case ArrivalPattern::POISSON: {
            std::exponential_distribution<double> exp_dist(config_.arrival_rate_rps);
            double interval_s = exp_dist(rng_);
            return static_cast<SimTime>(interval_s * 1'000'000'000);
        }
        case ArrivalPattern::UNIFORM: {
            std::uniform_real_distribution<double> dist(0, 2.0 / config_.arrival_rate_rps);
            double interval_s = dist(rng_);
            return static_cast<SimTime>(interval_s * 1'000'000'000);
        }
        case ArrivalPattern::FIXED_INTERVAL:
            return config_.interval_ns * index;
        case ArrivalPattern::BURST: {
            // Burst: 10 requests at start, then Poisson
            if (index < 10) return 0;
            std::exponential_distribution<double> exp_dist(config_.arrival_rate_rps);
            double interval_s = exp_dist(rng_);
            return static_cast<SimTime>(interval_s * 1'000'000'000);
        }
    }
    return 0;
}

uint32_t RequestGenerator::random_prompt_tokens() {
    std::uniform_int_distribution<uint32_t> dist(
        config_.prompt_tokens_min, config_.prompt_tokens_max);
    return dist(rng_);
}

uint32_t RequestGenerator::random_output_tokens() {
    std::uniform_int_distribution<uint32_t> dist(
        config_.output_tokens_min, config_.output_tokens_max);
    return dist(rng_);
}

} // namespace atmos_sim
