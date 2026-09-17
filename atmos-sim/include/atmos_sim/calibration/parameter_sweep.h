#pragma once

#include "atmos_sim/calibration/calibration_database.h"
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>

namespace atmos_sim {

/// A single sweep point result.
struct SweepResult {
    std::unordered_map<std::string, double> parameter_values;
    std::unordered_map<std::string, double> output_metrics;
    uint32_t sample_index = 0;
};

/// Configuration for a parameter sweep.
struct SweepConfig {
    std::string parameter_name;
    double start_value;
    double end_value;
    uint32_t num_steps = 10;
    bool logarithmic = false;  // Log-spaced steps
};

/// Function that runs a simulation with given parameters and returns metrics.
using SweepSimulationFn = std::function<std::unordered_map<std::string, double>(
    const std::unordered_map<std::string, double>& params)>;

/// Parameter sweep engine — runs simulations across parameter ranges
/// to identify sensitivity and confidence in conclusions.
class ParameterSweep {
public:
    ParameterSweep();

    /// Run a single-parameter sweep.
    std::vector<SweepResult> sweep_single(
        const SweepConfig& config,
        const CalibrationDatabase& base_params,
        SweepSimulationFn sim_fn);

    /// Run a multi-parameter sweep (Cartesian product).
    std::vector<SweepResult> sweep_multi(
        const std::vector<SweepConfig>& configs,
        const CalibrationDatabase& base_params,
        SweepSimulationFn sim_fn);

    /// Run Monte-Carlo sensitivity analysis.
    std::vector<SweepResult> monte_carlo(
        const CalibrationDatabase& params,
        uint32_t num_samples,
        uint32_t base_seed,
        SweepSimulationFn sim_fn);

    /// Analyze sensitivity: which parameter most affects the output?
    struct SensitivityResult {
        std::string parameter_name;
        double correlation;  // -1 to 1
        double max_impact;   // Max output variation caused
    };
    std::vector<SensitivityResult> analyze_sensitivity(
        const std::vector<SweepResult>& results,
        const std::string& output_metric);

private:
    std::vector<double> linspace(double start, double end, uint32_t steps);
    std::vector<double> logspace(double start, double end, uint32_t steps);
};

} // namespace atmos_sim
