#include "atmos_sim/calibration/calibration_database.h"
#include <sstream>
#include <random>
#include <algorithm>

namespace atmos_sim {

const char* confidence_name(Confidence c) {
    switch (c) {
        case Confidence::LOW:      return "LOW";
        case Confidence::MEDIUM:   return "MEDIUM";
        case Confidence::HIGH:     return "HIGH";
        case Confidence::MEASURED: return "MEASURED";
    }
    return "UNKNOWN";
}

CalibrationDatabase::CalibrationDatabase() = default;

void CalibrationDatabase::set_parameter(const CalibrationParameter& param) {
    params_[param.name] = param;
}

const CalibrationParameter* CalibrationDatabase::get_parameter(const std::string& name) const {
    auto it = params_.find(name);
    if (it == params_.end()) return nullptr;
    return &it->second;
}

double CalibrationDatabase::nominal(const std::string& name, double default_val) const {
    auto it = params_.find(name);
    if (it == params_.end()) return default_val;
    return it->second.nominal;
}

std::vector<std::string> CalibrationDatabase::parameter_names() const {
    std::vector<std::string> names;
    names.reserve(params_.size());
    for (const auto& [name, _] : params_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

const std::unordered_map<std::string, CalibrationParameter>& CalibrationDatabase::parameters() const {
    return params_;
}

std::unordered_map<std::string, double> CalibrationDatabase::monte_carlo_sample(uint32_t seed) const {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(0.0, 1.0);

    std::unordered_map<std::string, double> sample;
    for (const auto& [name, param] : params_) {
        double t = dist(rng);
        sample[name] = param.low + t * (param.high - param.low);
    }
    return sample;
}

std::string CalibrationDatabase::to_json() const {
    std::ostringstream oss;
    oss << "{\n  \"parameters\": {\n";
    auto names = parameter_names();
    for (size_t i = 0; i < names.size(); ++i) {
        const auto& p = params_.at(names[i]);
        oss << "    \"" << p.name << "\": {"
            << "\"nominal\":" << p.nominal
            << ",\"low\":" << p.low
            << ",\"high\":" << p.high
            << ",\"confidence\":\"" << confidence_name(p.confidence) << "\""
            << ",\"unit\":\"" << p.unit << "\""
            << "}";
        if (i + 1 < names.size()) oss << ",";
        oss << "\n";
    }
    oss << "  }\n}\n";
    return oss.str();
}

CalibrationDatabase CalibrationDatabase::from_json(const std::string& /*json*/) {
    return CalibrationDatabase();
}

CalibrationDatabase CalibrationDatabase::default_atmos_params() {
    CalibrationDatabase db;

    db.set_parameter({"pcie_effective_bw", "bytes/sec", 32e9, 28e9, 35e9,
                      Confidence::MEDIUM, ParameterSource::DATASHEET,
                      "PCIe Gen4 x8 effective bandwidth"});

    db.set_parameter({"dma_setup_overhead", "ns", 200, 100, 400,
                      Confidence::LOW, ParameterSource::ESTIMATED,
                      "DMA descriptor setup overhead"});

    db.set_parameter({"dma_bandwidth", "bytes/sec", 32e9, 28e9, 36e9,
                      Confidence::MEDIUM, ParameterSource::DATASHEET,
                      "DMA engine bandwidth"});

    db.set_parameter({"hbf_read_bw_per_channel", "bytes/sec", 14e9, 10e9, 18e9,
                      Confidence::LOW, ParameterSource::ESTIMATED,
                      "HBF read bandwidth per channel"});

    db.set_parameter({"hbf_protocol_overhead", "ns", 500, 200, 1000,
                      Confidence::LOW, ParameterSource::ESTIMATED,
                      "HBF base protocol overhead"});

    db.set_parameter({"lpddr_bw_per_channel", "bytes/sec", 6e9, 5e9, 7e9,
                      Confidence::MEDIUM, ParameterSource::DATASHEET,
                      "LPDDR bandwidth per channel"});

    db.set_parameter({"npu_peak_flops", "FLOPS", 500e9, 400e9, 600e9,
                      Confidence::LOW, ParameterSource::ESTIMATED,
                      "NPU peak compute throughput"});

    db.set_parameter({"pcie_switch_latency", "ns", 200, 100, 400,
                      Confidence::MEDIUM, ParameterSource::DATASHEET,
                      "PCIe switch transit latency"});

    db.set_parameter({"hbf_controller_efficiency", "ratio", 0.85, 0.70, 0.95,
                      Confidence::LOW, ParameterSource::ESTIMATED,
                      "HBF controller efficiency factor"});

    return db;
}

} // namespace atmos_sim
