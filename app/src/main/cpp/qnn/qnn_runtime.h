#pragma once

#include "qnn_backend_info.h"

#include <string>

namespace phonelm::qnn {

enum class QnnBackendKind {
    CPU,
    HTP,
};

const char* backendKindName(QnnBackendKind kind);

class Runtime {
public:
    Runtime();
    const BackendInfo& info() const;
    bool initialize(QnnBackendKind requestedBackend, std::string& error);

private:
    BackendInfo info_;
};

}  // namespace phonelm::qnn
