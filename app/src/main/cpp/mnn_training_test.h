#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace phonelm {

enum class BackendKind : int {
    CPU = 0,
    OPENCL = 1,
    VULKAN = 2,
};

struct TrainingConfig {
    BackendKind backend = BackendKind::CPU;
    int batchSize = 8;
    int dimension = 128;
    int hiddenDimension = 128;
    int outputDimension = 64;
    int steps = 100;
    int warmupSteps = 0;
    float learningRate = 0.1f;
    std::uint64_t seed = 20260710;
    int sampleCount = 512;
    int epochs = 0;
    int measuredSteps = 0;
    int correctnessInterval = 0;
    bool benchmarkMode = false;
};

struct TrainingOutcome {
    std::string backendRequested;
    std::string backendActual = "UNINITIALIZED";
    std::string executedBackends = "NONE";
    std::string status = "FAILED";
    std::string error;
    std::string fallbackOperations;
    float initialLoss = 0.0f;
    float finalLoss = 0.0f;
    double averageStepTimeMs = 0.0;
    double medianStepTimeMs = 0.0;
    double p95StepTimeMs = 0.0;
    double totalTimeMs = 0.0;
    int completedSteps = 0;
    bool lossDecreased = false;
    bool nanDetected = false;
    bool weightsChanged = false;
    bool fallbackDetected = false;
    bool requestedBackendObserved = false;
    // MNN 3.5.0 SGD::step intentionally materializes gradients and parameters
    // through readMap()/input(). This is reported instead of being presented as
    // a device-resident optimizer.
    bool optimizerHostSync = true;
    std::vector<double> measuredStepTimesMs;
};

using LogSink = std::function<void(const std::string&)>;

const char* backendName(BackendKind backend);
bool validateTrainingConfig(const TrainingConfig& config, std::string& error);
TrainingOutcome runMnnTraining(const TrainingConfig& config,
                               std::atomic_bool& stopRequested,
                               const LogSink& log);
std::string mnnEnvironmentReport();

}  // namespace phonelm
