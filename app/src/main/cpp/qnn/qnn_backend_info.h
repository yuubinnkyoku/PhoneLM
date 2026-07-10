#pragma once

#include <string>

namespace phonelm::qnn {

struct BackendInfo {
    bool qnnBuildEnabled = false;
    bool sdkDetected = false;
    bool implementationReady = false;
    std::string sdkVersion = "NOT_AVAILABLE";
    std::string apiVersion = "NOT_AVAILABLE";
    std::string status = "QNN_SDK_NOT_FOUND";
    std::string sdkRoot = "NOT_SET";

    std::string toLogString() const;
};

BackendInfo queryBackendInfo();

}  // namespace phonelm::qnn

