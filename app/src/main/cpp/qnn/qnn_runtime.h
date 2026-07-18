#pragma once

#include "qnn_backend_info.h"

#include <cstdint>
#include <string>
#include <vector>

namespace phonelm::qnn {

enum class QnnBackendKind {
    CPU,
    HTP,
};

const char* backendKindName(QnnBackendKind kind);

struct RuntimeMetrics {
    std::uint64_t graphCreateCount = 0;
    std::uint64_t graphFinalizeCount = 0;
    std::uint64_t graphExecuteCount = 0;
    std::uint64_t runtimeWeightUpdateCount = 0;
    double backendCreateUs = 0.0;
    double deviceCreateUs = 0.0;
    double contextCreateUs = 0.0;
    double graphCreateUs = 0.0;
    double graphFinalizeUs = 0.0;
    std::vector<double> executeUs;
    std::vector<double> weightUpdateUs;
};
class Runtime {
public:
    Runtime();
    ~Runtime();
    const BackendInfo& info() const;
    const std::string& diagnostics() const;
    bool initialize(QnnBackendKind requestedBackend, std::string& error);
    bool prepareMatMul(uint32_t m, uint32_t k, uint32_t n, bool transposeInput0,
                       std::string& error);
    bool executeMatMul(const std::vector<float>& a, const std::vector<float>& b,
                       std::vector<float>& output, std::string& error);
    bool setInitialWeight(const std::vector<float>& weight, std::string& error);
    bool updateWeight(const std::vector<float>& weight, std::string& error);
    bool executePrepared(const std::vector<float>& input, std::vector<float>& output,
                         std::string& error);
    const RuntimeMetrics& metrics() const;

private:
    BackendInfo info_;
    std::string diagnostics_;
    RuntimeMetrics metrics_;
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace phonelm::qnn
