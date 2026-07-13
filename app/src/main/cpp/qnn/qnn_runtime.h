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

private:
    BackendInfo info_;
    std::string diagnostics_;
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace phonelm::qnn
