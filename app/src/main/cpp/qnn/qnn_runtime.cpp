#include "qnn_runtime.h"

namespace phonelm::qnn {

Runtime::Runtime() : info_(queryBackendInfo()) {}

const BackendInfo& Runtime::info() const {
    return info_;
}

bool Runtime::initialize(const std::string& requestedBackend, std::string& error) {
    if (!info_.sdkDetected) {
        error = "QNN_SDK_NOT_FOUND: cannot initialize " + requestedBackend;
        return false;
    }
    error = "NOT_IMPLEMENTED: installed SDK headers and official samples must be audited first";
    return false;
}

}  // namespace phonelm::qnn

