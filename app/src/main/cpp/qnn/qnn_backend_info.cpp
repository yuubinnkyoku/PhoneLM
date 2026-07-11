#include "qnn_backend_info.h"

#include <sstream>

#ifndef PHONELM_ENABLE_QNN
#define PHONELM_ENABLE_QNN 0
#endif

#ifndef PHONELM_QAIRT_SDK_DETECTED
#define PHONELM_QAIRT_SDK_DETECTED 0
#endif

#ifndef PHONELM_QAIRT_SDK_ROOT_TEXT
#define PHONELM_QAIRT_SDK_ROOT_TEXT "NOT_SET"
#endif

namespace phonelm::qnn {

std::string BackendInfo::toLogString() const {
    std::ostringstream stream;
    stream << "qnn_enabled=" << (qnnBuildEnabled ? "true" : "false") << '\n'
           << "qnn_sdk_detected=" << (sdkDetected ? "true" : "false") << '\n'
           << "qnn_implementation_ready=" << (implementationReady ? "true" : "false") << '\n'
           << "qnn_status=" << status << '\n'
           << "qnn_sdk_version=" << sdkVersion << '\n'
           << "qnn_api_version=" << apiVersion << '\n'
           << "qairt_sdk_root=" << sdkRoot;
    return stream.str();
}

BackendInfo queryBackendInfo() {
    BackendInfo info;
    info.qnnBuildEnabled = PHONELM_ENABLE_QNN != 0;
    info.sdkDetected = PHONELM_QAIRT_SDK_DETECTED != 0;
    info.sdkRoot = PHONELM_QAIRT_SDK_ROOT_TEXT;
    // No QAIRT SDK was present in the development environment. Therefore no
    // QNN symbol, version struct, backend library, or data type is invented.
    info.implementationReady = false;
    if (!info.sdkDetected) {
        info.status = "BLOCKED_BY_QAIRT_SDK_NOT_INSTALLED";
    } else if (!info.qnnBuildEnabled) {
        info.status = "QNN_DISABLED";
    } else {
        info.status = "NOT_IMPLEMENTED_FOR_UNVERIFIED_SDK";
    }
    return info;
}

}  // namespace phonelm::qnn
