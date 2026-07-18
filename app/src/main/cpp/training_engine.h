#pragma once

#include "mnn_training_test.h"

#include <atomic>
#include <string>

namespace phonelm {

enum class ExecutionMode : int {
    CPU_REFERENCE = 0,
    MNN_CPU = 1,
    MNN_OPENCL = 2,
    MNN_VULKAN = 3,
    QNN_CPU_FORWARD = 4,
    QNN_HTP_FORWARD = 5,
    QNN_HTP_FORWARD_CPU_BACKWARD = 6,
    QNN_HTP_FORWARD_DW = 7,
    QNN_HTP_FORWARD_DW_DX = 8,
    QNN_HTP_FULL_STEP = 9,
    QNN_HTP_DEVICE_PROBE = 10,
    QNN_CPU_LINEAR_TRAINING = 11,
    QNN_HTP_LINEAR_TRAINING = 12,
    QNN_LINEAR_GRADIENT_CHECK = 13,
    QNN_CPU_MULTIBATCH_TRAINING = 14,
    QNN_HTP_MULTIBATCH_TRAINING = 15,
    QNN_CPU_TRAINING_BENCHMARK = 16,
    QNN_HTP_TRAINING_BENCHMARK = 17,
};

const char* executionModeName(ExecutionMode mode);

class TrainingEngine {
public:
    static std::string run(ExecutionMode mode,
                           const TrainingConfig& config,
                           std::atomic_bool& stopRequested,
                           const LogSink& log);
    static std::string capabilityReport();
};

}  // namespace phonelm
