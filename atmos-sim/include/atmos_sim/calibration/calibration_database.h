#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace atmos_sim {

/// Confidence level for a calibration parameter.
enum class Confidence : uint8_t {
    LOW,
    MEDIUM,
    HIGH,
    MEASURED,
};

const char* confidence_name(Confidence c);

/// Source of a calibration value.
enum class ParameterSource : uint8_t {
    ESTIMATED,
    DATASHEET,
    MEASURED_PROXY,
    MEASURED_DIRECT,
    RTL_CORRELATED,
};

/// A single calibration parameter with uncertainty bounds.
struct CalibrationParameter {
    std::string name;
    std::string unit;
    double nominal = 0.0;
    double low = 0.0;      // Lower bound of confidence range
    double high = 0.0;     // Upper bound of confidence range
    Confidence confidence = Confidence::LOW;
    ParameterSource source = ParameterSource::ESTIMATED;
    std::string notes;
};

/// Calibration database — stores hardware parameters with confidence ranges.
class CalibrationDatabase {
public:
    CalibrationDatabase();

    /// Add or update a parameter.
    void set_parameter(const CalibrationParameter& param);

    /// Get a parameter by name. Returns nullptr if not found.
    const CalibrationParameter* get_parameter(const std::string& name) const;

    /// Get nominal value (or default if not found).
    double nominal(const std::string& name, double default_val = 0.0) const;

    /// Get all parameter names.
    std::vector<std::string> parameter_names() const;

    /// Get all parameters.
    const std::unordered_map<std::string, CalibrationParameter>& parameters() const;

    /// Generate a Monte-Carlo sample: each parameter drawn from [low, high].
    std::unordered_map<std::string, double> monte_carlo_sample(uint32_t seed) const;

    /// Serialize to JSON.
    std::string to_json() const;

    /// Load from JSON.
    static CalibrationDatabase from_json(const std::string& json);

    /// Load default ATMOS parameters.
    static CalibrationDatabase default_atmos_params();

private:
    std::unordered_map<std::string, CalibrationParameter> params_;
};

} // namespace atmos_sim
