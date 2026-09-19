#include "atmos_sim/model_graph/tokensim_adapter.h"
#include <algorithm>
#include <utility>

namespace atmos_sim {

TokenSimAdapter::TokenSimAdapter(TokenSimAdapterConfig config)
    : config_(std::move(config)) {}

std::vector<TimedRequest> TokenSimAdapter::to_workload(const std::vector<TokenSimEvent>& events) const {
    std::vector<TimedRequest> workload;
    workload.reserve(events.size());

    for (const auto& event : events) {
        if (event.type != TokenSimEventType::REQUEST_ARRIVED) {
            continue;
        }

        TimedRequest request;
        request.arrival_time = event.timestamp;
        request.request_id = event.request_id;
        request.shape = config_.default_shape;
        if (event.prompt_tokens > 0) {
            request.shape.prompt_tokens = event.prompt_tokens;
        }
        if (event.output_tokens > 0) {
            request.shape.max_output_tokens = event.output_tokens;
        }
        if (!event.model_name.empty()) {
            request.shape.model_name = event.model_name;
        }
        request.shape.priority = event.priority;
        workload.push_back(std::move(request));
    }

    std::sort(workload.begin(), workload.end(),
              [](const TimedRequest& lhs, const TimedRequest& rhs) {
                  if (lhs.arrival_time != rhs.arrival_time) {
                      return lhs.arrival_time < rhs.arrival_time;
                  }
                  return lhs.request_id < rhs.request_id;
              });

    return workload;
}

} // namespace atmos_sim