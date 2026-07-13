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
    info.implementationReady = info.qnnBuildEnabled && info.sdkDetected;
    if (info.sdkDetected) {
        info.sdkVersion = "2.48.40.260702";
        info.apiVersion = "2.37.0";
    }
    if (!info.sdkDetected) {
        info.status = "BLOCKED_BY_QAIRT_SDK_NOT_INSTALLED";
    } else if (!info.qnnBuildEnabled) {
        info.status = "QNN_DISABLED";
    } else {
        info.status = "QAIRT_ADAPTER_READY_REQUIRES_DEVICE_EXECUTION";
    }
    return info;
}

}  // namespace phonelm::qnn
