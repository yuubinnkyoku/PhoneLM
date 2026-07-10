#pragma once

#include "qnn_backend_info.h"

#include <string>

namespace phonelm::qnn {

class Runtime {
public:
    Runtime();
    const BackendInfo& info() const;
    bool initialize(const std::string& requestedBackend, std::string& error);

private:
    BackendInfo info_;
};

}  // namespace phonelm::qnn

