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

const BackendInfo& Runtime::info() const {
    return info_;
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
