#include "qnn_runtime.h"

namespace phonelm::qnn {

const char* backendKindName(QnnBackendKind kind) {
    switch (kind) {
        case QnnBackendKind::CPU: return "CPU";
        case QnnBackendKind::HTP: return "HTP";
    }
    return "UNKNOWN";
}

Runtime::Runtime() : info_(queryBackendInfo()) {}
Runtime::~Runtime() = default;

const BackendInfo& Runtime::info() const {
    return info_;
}
const std::string& Runtime::diagnostics() const { return diagnostics_; }
const RuntimeMetrics& Runtime::metrics() const { return metrics_; }

bool Runtime::prepareMatMul(uint32_t, uint32_t, uint32_t, bool, std::string& error) {
    error = "QNN_DISABLED: MatMul unavailable"; return false;
}
bool Runtime::executeMatMul(const std::vector<float>&, const std::vector<float>&,
                            std::vector<float>&, std::string& error) {
    error = "QNN_DISABLED: MatMul unavailable"; return false;
}bool Runtime::setInitialWeight(const std::vector<float>&, std::string& error) {
    error = "QNN_DISABLED: weight binding unavailable"; return false;
}
bool Runtime::updateWeight(const std::vector<float>&, std::string& error) {
    error = "QNN_DISABLED: weight update unavailable"; return false;
}
bool Runtime::executePrepared(const std::vector<float>&, std::vector<float>&,
                              std::string& error) {
    error = "QNN_DISABLED: MatMul unavailable"; return false;
}

bool Runtime::initialize(QnnBackendKind requestedBackend, std::string& error) {
    const std::string backend = backendKindName(requestedBackend);
    if (!info_.sdkDetected) {
        error = "QNN_SDK_NOT_FOUND: cannot initialize " + backend;
        return false;
    }
    if (!info_.qnnBuildEnabled) {
        error = "QNN_DISABLED: cannot initialize " + backend;
        return false;
    }
    error = "NOT_IMPLEMENTED: qnn_runtime_qairt.cpp is unavailable until the installed "
            "SDK headers and official samples are audited";
    return false;
}

}  // namespace phonelm::qnn
