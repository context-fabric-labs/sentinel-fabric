#include "atmos_sim/calibration/parameter_sweep.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace atmos_sim {

ParameterSweep::ParameterSweep() = default;

std::vector<SweepResult> ParameterSweep::sweep_single(
    const SweepConfig& config,
    const CalibrationDatabase& base_params,
    SweepSimulationFn sim_fn) {

    std::vector<SweepResult> results;
    auto values = config.logarithmic
        ? logspace(config.start_value, config.end_value, config.num_steps)
        : linspace(config.start_value, config.end_value, config.num_steps);

    for (uint32_t i = 0; i < values.size(); ++i) {
        auto params = base_params.monte_carlo_sample(i);
        params[config.parameter_name] = values[i];

        SweepResult result;
        result.parameter_values = params;
        result.output_metrics = sim_fn(params);
        result.sample_index = i;
        results.push_back(std::move(result));
    }
    return results;
}

std::vector<SweepResult> ParameterSweep::sweep_multi(
    const std::vector<SweepConfig>& configs,
    const CalibrationDatabase& base_params,
    SweepSimulationFn sim_fn) {

    // Generate Cartesian product of all parameter values
    std::vector<std::vector<double>> all_values;
    for (const auto& config : configs) {
        all_values.push_back(config.logarithmic
            ? logspace(config.start_value, config.end_value, config.num_steps)
            : linspace(config.start_value, config.end_value, config.num_steps));
    }

    std::vector<SweepResult> results;
    // Simple nested iteration for small number of parameters
    std::function<void(size_t, std::unordered_map<std::string, double>)> recurse;
    uint32_t idx = 0;
    recurse = [&](size_t param_idx, std::unordered_map<std::string, double> current) {
        if (param_idx == configs.size()) {
            SweepResult result;
            result.parameter_values = current;
            result.output_metrics = sim_fn(current);
            result.sample_index = idx++;
            results.push_back(std::move(result));
            return;
        }
        for (double val : all_values[param_idx]) {
            auto next = current;
            next[configs[param_idx].parameter_name] = val;
            recurse(param_idx + 1, next);
        }
    };

    auto base = base_params.monte_carlo_sample(0);
    recurse(0, base);
    return results;
}

std::vector<SweepResult> ParameterSweep::monte_carlo(
    const CalibrationDatabase& params,
    uint32_t num_samples,
    uint32_t base_seed,
    SweepSimulationFn sim_fn) {

    std::vector<SweepResult> results;
    for (uint32_t i = 0; i < num_samples; ++i) {
        auto param_values = params.monte_carlo_sample(base_seed + i);

        SweepResult result;
        result.parameter_values = param_values;
        result.output_metrics = sim_fn(param_values);
        result.sample_index = i;
        results.push_back(std::move(result));
    }
    return results;
}

std::vector<ParameterSweep::SensitivityResult> ParameterSweep::analyze_sensitivity(
    const std::vector<SweepResult>& results,
    const std::string& output_metric) {

    if (results.empty()) return {};

    // Collect parameter names
    std::vector<std::string> param_names;
    for (const auto& [name, _] : results[0].parameter_values) {
        param_names.push_back(name);
    }

    std::vector<SensitivityResult> sensitivity;

    for (const auto& param_name : param_names) {
        // Collect x (parameter) and y (output) values
        std::vector<double> x_vals, y_vals;
        double y_min = 1e18, y_max = -1e18;

        for (const auto& result : results) {
            auto param_it = result.parameter_values.find(param_name);
            auto metric_it = result.output_metrics.find(output_metric);
            if (param_it != result.parameter_values.end() &&
                metric_it != result.output_metrics.end()) {
                x_vals.push_back(param_it->second);
                y_vals.push_back(metric_it->second);
                y_min = std::min(y_min, metric_it->second);
                y_max = std::max(y_max, metric_it->second);
            }
        }

        if (x_vals.size() < 2) continue;

        // Simple Pearson correlation
        double n = static_cast<double>(x_vals.size());
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
        for (size_t i = 0; i < x_vals.size(); ++i) {
            sum_x += x_vals[i];
            sum_y += y_vals[i];
            sum_xy += x_vals[i] * y_vals[i];
            sum_x2 += x_vals[i] * x_vals[i];
            sum_y2 += y_vals[i] * y_vals[i];
        }

        double denom = std::sqrt((n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y));
        double correlation = (denom > 0) ? (n * sum_xy - sum_x * sum_y) / denom : 0.0;

        sensitivity.push_back({param_name, correlation, y_max - y_min});
    }

    // Sort by absolute correlation
    std::sort(sensitivity.begin(), sensitivity.end(),
              [](const SensitivityResult& a, const SensitivityResult& b) {
                  return std::abs(a.correlation) > std::abs(b.correlation);
              });

    return sensitivity;
}

std::vector<double> ParameterSweep::linspace(double start, double end, uint32_t steps) {
    std::vector<double> result;
    if (steps <= 1) return {start};
    double step = (end - start) / (steps - 1);
    for (uint32_t i = 0; i < steps; ++i) {
        result.push_back(start + i * step);
    }
    return result;
}

std::vector<double> ParameterSweep::logspace(double start, double end, uint32_t steps) {
    std::vector<double> result;
    if (steps <= 1) return {start};
    double log_start = std::log10(start);
    double log_end = std::log10(end);
    double log_step = (log_end - log_start) / (steps - 1);
    for (uint32_t i = 0; i < steps; ++i) {
        result.push_back(std::pow(10.0, log_start + i * log_step));
    }
    return result;
}

} // namespace atmos_sim
