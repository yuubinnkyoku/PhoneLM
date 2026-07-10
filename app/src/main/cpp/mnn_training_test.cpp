#include "mnn_training_test.h"

#include <MNN/Interpreter.hpp>
#include <MNN/expr/Executor.hpp>
#include <MNN/expr/ExecutorScope.hpp>
#include <MNN/expr/ExprCreator.hpp>
#include <MNN/expr/Module.hpp>

#include "SGD.hpp"
#include "core/Backend.hpp"
#include "core/TensorUtils.hpp"

#include <android/api-level.h>
#include <sys/system_properties.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace phonelm {
namespace {

using Clock = std::chrono::steady_clock;
using MNN::Express::Executor;
using MNN::Express::ExecutorScope;
using MNN::Express::Module;
using MNN::Express::VARP;
using namespace MNN::Express;

constexpr const char* kMnnCommit = "c35f14f3ab5cb65094863b9a0e888370b027a670";
constexpr std::size_t kMaxEstimatedWorkingSetBytes = 1536ULL * 1024ULL * 1024ULL;

MNNForwardType toMnnBackend(BackendKind backend) {
    switch (backend) {
        case BackendKind::OPENCL:
            return MNN_FORWARD_OPENCL;
        case BackendKind::VULKAN:
            return MNN_FORWARD_VULKAN;
        case BackendKind::CPU:
        default:
            return MNN_FORWARD_CPU;
    }
}

const char* mnnBackendName(MNNForwardType backend) {
    switch (backend) {
        case MNN_FORWARD_OPENCL:
            return "OPENCL";
        case MNN_FORWARD_VULKAN:
            return "VULKAN";
        case MNN_FORWARD_CPU:
            return "CPU";
        default:
            return "OTHER";
    }
}

std::string systemProperty(const char* name, const char* fallback) {
    char value[PROP_VALUE_MAX] = {};
    const int length = __system_property_get(name, value);
    return length > 0 ? std::string(value, static_cast<std::size_t>(length))
                      : std::string(fallback);
}

std::string deviceName() {
    const auto manufacturer = systemProperty("ro.product.manufacturer", "unknown");
    const auto model = systemProperty("ro.product.model", "unknown");
    return manufacturer + " " + model;
}

std::string cpuAbi() {
#if defined(__aarch64__)
    return "arm64-v8a";
#else
    return "unsupported-abi";
#endif
}

std::string boolString(bool value) {
    return value ? "true" : "false";
}

std::string lossString(float value) {
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

std::string timeString(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

double elapsedMilliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool checkedMultiply(std::size_t lhs, std::size_t rhs, std::size_t& output) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        return false;
    }
    output = lhs * rhs;
    return true;
}

std::string safeOperatorText(const MNN::OperatorInfo* info) {
    if (info == nullptr) {
        return "unnamed(unknown)";
    }
    std::string name = info->name().empty() ? "unnamed" : info->name();
    std::string type = info->type().empty() ? "unknown" : info->type();
    for (char& character : name) {
        if (character == ',' || character == ';' || character == '\n' || character == '\r') {
            character = '_';
        }
    }
    for (char& character : type) {
        if (character == ',' || character == ';' || character == '\n' || character == '\r') {
            character = '_';
        }
    }
    return name + "(" + type + ")";
}

class BackendTracker {
public:
    explicit BackendTracker(MNNForwardType requested) : requested_(requested) {}

    void setPhase(std::string phase) {
        phase_ = std::move(phase);
    }

    bool after(const std::vector<MNN::Tensor*>& outputs, const MNN::OperatorInfo* info) {
        std::set<MNNForwardType> operationBackends;
        for (const auto* tensor : outputs) {
            if (tensor == nullptr) {
                continue;
            }
            const auto* description = MNN::TensorUtils::getDescribeOrigin(tensor);
            if (description == nullptr || description->getBackend() == nullptr) {
                continue;
            }
            operationBackends.insert(description->getBackend()->type());
        }

        for (const auto backend : operationBackends) {
            executed_.insert(backend);
            if (backend == requested_) {
                requestedObserved_ = true;
            }
            if (requested_ != MNN_FORWARD_CPU && backend == MNN_FORWARD_CPU) {
                fallbackOperations_.insert(phase_ + ":" + safeOperatorText(info));
            }
        }
        return true;
    }

    bool requestedObserved() const {
        return requestedObserved_;
    }

    bool fallbackDetected() const {
        return !fallbackOperations_.empty() ||
               (requested_ != MNN_FORWARD_CPU && !requestedObserved_);
    }

    std::string executedBackends() const {
        if (executed_.empty()) {
            return "NONE_OBSERVED";
        }
        std::ostringstream stream;
        bool first = true;
        for (const auto backend : executed_) {
            if (!first) {
                stream << ',';
            }
            first = false;
            stream << mnnBackendName(backend);
        }
        return stream.str();
    }

    std::string fallbackOperations() const {
        if (requested_ != MNN_FORWARD_CPU && !requestedObserved_ && fallbackOperations_.empty()) {
            return "requested_backend_not_observed";
        }
        if (fallbackOperations_.empty()) {
            return "none";
        }
        std::ostringstream stream;
        std::size_t count = 0;
        for (const auto& operation : fallbackOperations_) {
            if (count > 0) {
                stream << ';';
            }
            stream << operation;
            ++count;
            if (count == 32 && fallbackOperations_.size() > count) {
                stream << ";...(" << (fallbackOperations_.size() - count) << " more)";
                break;
            }
        }
        return stream.str();
    }

private:
    MNNForwardType requested_;
    std::string phase_ = "UNSPECIFIED";
    std::set<MNNForwardType> executed_;
    std::set<std::string> fallbackOperations_;
    bool requestedObserved_ = false;
};

VARP makeLoss(const VARP& x, const VARP& expected, const VARP& weight,
              const std::string& prefix) {
    auto prediction = _MatMul(x, weight, false, false);
    prediction->setName(prefix + "_matmul");
    auto difference = _Subtract(prediction, expected);
    difference->setName(prefix + "_subtract");
    auto squared = _Square(difference);
    squared->setName(prefix + "_square");
    auto loss = _ReduceMean(squared, {}, false);
    loss->setName(prefix + "_reduce_mean");
    return loss;
}

bool readScalarLoss(const VARP& loss, float& value) {
    const auto* pointer = loss->readMap<float>();
    if (pointer == nullptr) {
        return false;
    }
    value = pointer[0];
    return true;
}

void computeStatistics(TrainingOutcome& outcome) {
    if (outcome.measuredStepTimesMs.empty()) {
        return;
    }
    double sum = 0.0;
    for (const auto time : outcome.measuredStepTimesMs) {
        sum += time;
    }
    outcome.averageStepTimeMs = sum / static_cast<double>(outcome.measuredStepTimesMs.size());

    auto sorted = outcome.measuredStepTimesMs;
    std::sort(sorted.begin(), sorted.end());
    const auto middle = sorted.size() / 2;
    outcome.medianStepTimeMs = sorted.size() % 2 == 0
                                   ? (sorted[middle - 1] + sorted[middle]) * 0.5
                                   : sorted[middle];
    const auto p95Index = static_cast<std::size_t>(
        std::ceil(0.95 * static_cast<double>(sorted.size()))) - 1;
    outcome.p95StepTimeMs = sorted[std::min(p95Index, sorted.size() - 1)];
}

std::string makeHeader(const TrainingConfig& config, const TrainingOutcome& outcome) {
    std::ostringstream stream;
    stream << "backend_requested=" << outcome.backendRequested << '\n'
           << "backend_actual=" << outcome.backendActual << '\n'
           << "mnn_version=" << MNN::getVersion() << '\n'
           << "mnn_commit=" << kMnnCommit << '\n'
           << "device_name=" << deviceName() << '\n'
           << "android_version=" << systemProperty("ro.build.version.release", "unknown") << '\n'
           << "android_api_level=" << android_get_device_api_level() << '\n'
           << "cpu_abi=" << cpuAbi() << '\n'
           << "batch_size=" << config.batchSize << '\n'
           << "dimension=" << config.dimension << '\n'
           << "steps=" << config.steps << '\n'
           << "warmup_steps=" << config.warmupSteps << '\n'
           << "learning_rate=" << config.learningRate << '\n'
           << "seed=" << config.seed << '\n'
           << "optimizer=SGD\n"
           << "optimizer_host_sync=true\n"
           << "fallback_detection=per_op_output_backend_callback";
    return stream.str();
}

}  // namespace

const char* backendName(BackendKind backend) {
    switch (backend) {
        case BackendKind::OPENCL:
            return "OPENCL";
        case BackendKind::VULKAN:
            return "VULKAN";
        case BackendKind::CPU:
        default:
            return "CPU";
    }
}

bool validateTrainingConfig(const TrainingConfig& config, std::string& error) {
    if (config.backend != BackendKind::CPU && config.backend != BackendKind::OPENCL &&
        config.backend != BackendKind::VULKAN) {
        error = "backend must be CPU(0), OPENCL(1), or VULKAN(2)";
        return false;
    }
    if (config.batchSize <= 0 || config.batchSize > 4096) {
        error = "batchSize must be in 1..4096";
        return false;
    }
    if (config.dimension <= 0 || config.dimension > 4096) {
        error = "dimension must be in 1..4096";
        return false;
    }
    if (config.steps <= 0 || config.steps > 100000) {
        error = "steps must be in 1..100000";
        return false;
    }
    if (config.warmupSteps < 0 || config.warmupSteps > 10000) {
        error = "warmupSteps must be in 0..10000";
        return false;
    }
    if (!std::isfinite(config.learningRate) || config.learningRate <= 0.0f ||
        config.learningRate > 10.0f) {
        error = "learningRate must be finite and in (0, 10]";
        return false;
    }

    std::size_t matrixElements = 0;
    std::size_t batchElements = 0;
    if (!checkedMultiply(static_cast<std::size_t>(config.dimension),
                         static_cast<std::size_t>(config.dimension), matrixElements) ||
        !checkedMultiply(static_cast<std::size_t>(config.batchSize),
                         static_cast<std::size_t>(config.dimension), batchElements)) {
        error = "tensor element count overflow";
        return false;
    }
    // Conservative allowance for forward tensors, generated gradient graph,
    // device copies, and MNN allocator workspaces.
    std::size_t estimatedElements = 0;
    if (!checkedMultiply(matrixElements * 3 + batchElements * 6, 12, estimatedElements) ||
        !checkedMultiply(estimatedElements, sizeof(float), estimatedElements) ||
        estimatedElements > kMaxEstimatedWorkingSetBytes) {
        error = "estimated working set exceeds 1536 MiB safety limit";
        return false;
    }
    return true;
}

TrainingOutcome runMnnTraining(const TrainingConfig& config,
                               std::atomic_bool& stopRequested,
                               const LogSink& log) {
    TrainingOutcome outcome;
    outcome.backendRequested = backendName(config.backend);
    const auto requestedMnnBackend = toMnnBackend(config.backend);

    const auto* creator = MNN::MNNGetExtraRuntimeCreator(requestedMnnBackend);
    if (creator == nullptr) {
        outcome.backendActual = "UNAVAILABLE";
        outcome.error = outcome.backendRequested + " runtime is not available on this device";
        return outcome;
    }

    MNN::BackendConfig backendConfig;
    backendConfig.precision = MNN::BackendConfig::Precision_High;
    backendConfig.power = MNN::BackendConfig::Power_High;
    backendConfig.memory = MNN::BackendConfig::Memory_Normal;
    const int executorMode = config.backend == BackendKind::CPU
                                 ? 4
                                 : (MNN_GPU_TUNING_WIDE |
                                    (config.backend == BackendKind::OPENCL
                                         ? MNN_GPU_MEMORY_BUFFER
                                         : 0));
    auto executor = Executor::newExecutor(requestedMnnBackend, backendConfig, executorMode);
    if (executor == nullptr) {
        outcome.backendActual = "INITIALIZATION_FAILED";
        outcome.error = "MNN Executor::newExecutor returned null";
        return outcome;
    }
    outcome.backendActual = outcome.backendRequested;

    // ExecutorScope is thread-local in MNN. It must live on this JNI worker
    // thread for every expression creation, gradient, and optimizer call.
    ExecutorScope executorScope("PhoneLMTraining", executor);
    BackendTracker tracker(requestedMnnBackend);
    executor->setCallBack(
        [](const std::vector<MNN::Tensor*>&, const MNN::OperatorInfo*) { return true; },
        [&tracker](const std::vector<MNN::Tensor*>& tensors, const MNN::OperatorInfo* info) {
            return tracker.after(tensors, info);
        });

    if (log) {
        log(makeHeader(config, outcome));
    }

    const std::size_t batchElements = static_cast<std::size_t>(config.batchSize) *
                                      static_cast<std::size_t>(config.dimension);
    const std::size_t matrixElements = static_cast<std::size_t>(config.dimension) *
                                       static_cast<std::size_t>(config.dimension);
    std::vector<float> xValues(batchElements);
    std::vector<float> targetWeights(matrixElements);
    std::vector<float> initialWeights(matrixElements);
    std::vector<float> expectedValues(batchElements, 0.0f);

    std::mt19937 generator(static_cast<std::mt19937::result_type>(config.seed));
    std::uniform_real_distribution<float> inputDistribution(-1.0f, 1.0f);
    std::uniform_real_distribution<float> targetDistribution(-0.25f, 0.25f);
    std::uniform_real_distribution<float> initialDistribution(-0.05f, 0.05f);
    for (auto& value : xValues) {
        value = inputDistribution(generator);
    }
    for (auto& value : targetWeights) {
        value = targetDistribution(generator);
    }
    for (auto& value : initialWeights) {
        value = initialDistribution(generator);
    }

    for (int batch = 0; batch < config.batchSize; ++batch) {
        for (int column = 0; column < config.dimension; ++column) {
            double sum = 0.0;
            for (int inner = 0; inner < config.dimension; ++inner) {
                sum += static_cast<double>(xValues[static_cast<std::size_t>(batch) * config.dimension + inner]) *
                       static_cast<double>(targetWeights[static_cast<std::size_t>(inner) * config.dimension + column]);
            }
            expectedValues[static_cast<std::size_t>(batch) * config.dimension + column] =
                static_cast<float>(sum);
        }
    }

    auto x = _Const(xValues.data(), {config.batchSize, config.dimension}, NHWC,
                    halide_type_of<float>());
    x->setName("X_fixed_seed");
    auto expected = _Const(expectedValues.data(), {config.batchSize, config.dimension}, NHWC,
                           halide_type_of<float>());
    expected->setName("Y_fixed_target");
    auto weight = _TrainableParam(initialWeights.data(), {config.dimension, config.dimension}, NHWC,
                                  halide_type_of<float>());
    weight->setName("W_trainable");

    std::shared_ptr<Module> module(Module::createEmpty({weight}));
    MNN::Train::SGD optimizer(module);
    optimizer.setLearningRate(config.learningRate);
    optimizer.setMomentum(0.0f);
    optimizer.setWeightDecay(0.0f);

    tracker.setPhase("INITIAL_FORWARD");
    auto initialLossVariable = makeLoss(x, expected, weight, "initial_forward");
    if (!readScalarLoss(initialLossVariable, outcome.initialLoss)) {
        outcome.error = "initial loss readMap failed";
        return outcome;
    }
    if (!std::isfinite(outcome.initialLoss)) {
        outcome.nanDetected = true;
        outcome.error = "initial loss is NaN or Inf";
        return outcome;
    }

    const auto trainingStart = Clock::now();
    bool computationFailed = false;
    float latestLoss = outcome.initialLoss;
    const int totalSteps = config.warmupSteps + config.steps;
    for (int combinedStep = 0; combinedStep < totalSteps; ++combinedStep) {
        if (stopRequested.load(std::memory_order_relaxed)) {
            outcome.status = "CANCELLED";
            outcome.error = "stop requested; stopped at a step boundary";
            break;
        }

        const bool warmup = combinedStep < config.warmupSteps;
        const int displayStep = warmup ? combinedStep + 1 : combinedStep - config.warmupSteps + 1;
        const auto stepStart = Clock::now();

        tracker.setPhase(warmup ? "WARMUP_FORWARD" : "MEASURE_FORWARD");
        auto loss = makeLoss(x, expected, weight,
                             (warmup ? "warmup_" : "measure_") + std::to_string(displayStep));
        if (!readScalarLoss(loss, latestLoss)) {
            outcome.error = "loss readMap failed at combined step " + std::to_string(combinedStep + 1);
            computationFailed = true;
            break;
        }
        if (!std::isfinite(latestLoss)) {
            outcome.nanDetected = true;
            outcome.error = "loss became NaN or Inf at combined step " + std::to_string(combinedStep + 1);
            computationFailed = true;
            break;
        }

        tracker.setPhase(warmup ? "WARMUP_BACKWARD_UPDATE" : "MEASURE_BACKWARD_UPDATE");
        if (!optimizer.step(loss)) {
            outcome.error = "MNN Train SGD::step failed at combined step " +
                            std::to_string(combinedStep + 1);
            computationFailed = true;
            break;
        }

        const auto stepEnd = Clock::now();
        const double stepTimeMs = elapsedMilliseconds(stepStart, stepEnd);
        ++outcome.completedSteps;
        if (!warmup) {
            outcome.measuredStepTimesMs.push_back(stepTimeMs);
        }

        if (log) {
            std::ostringstream progress;
            progress << "backend_requested=" << outcome.backendRequested << '\n'
                     << "backend_actual=" << outcome.backendActual << '\n'
                     << "batch_size=" << config.batchSize << '\n'
                     << "dimension=" << config.dimension << '\n'
                     << (warmup ? "warmup_step=" : "step=") << displayStep << '\n'
                     << "loss=" << lossString(latestLoss) << '\n'
                     << "step_time_ms=" << timeString(stepTimeMs) << '\n'
                     << "fallback_detected=" << boolString(tracker.fallbackDetected());
            log(progress.str());
        }
    }

    const auto trainingEnd = Clock::now();
    outcome.totalTimeMs = elapsedMilliseconds(trainingStart, trainingEnd);

    tracker.setPhase("FINAL_FORWARD");
    auto finalLossVariable = makeLoss(x, expected, weight, "final_forward");
    if (!readScalarLoss(finalLossVariable, outcome.finalLoss)) {
        if (outcome.error.empty()) {
            outcome.error = "final loss readMap failed";
        }
        computationFailed = true;
    } else if (!std::isfinite(outcome.finalLoss)) {
        outcome.nanDetected = true;
        if (outcome.error.empty()) {
            outcome.error = "final loss is NaN or Inf";
        }
        computationFailed = true;
    }

    const auto* finalWeightPointer = weight->readMap<float>();
    if (finalWeightPointer == nullptr) {
        if (outcome.error.empty()) {
            outcome.error = "final weight readMap failed";
        }
        computationFailed = true;
    } else {
        for (std::size_t index = 0; index < matrixElements; ++index) {
            if (std::fabs(finalWeightPointer[index] - initialWeights[index]) > 1.0e-8f) {
                outcome.weightsChanged = true;
                break;
            }
        }
    }

    outcome.lossDecreased = std::isfinite(outcome.initialLoss) && std::isfinite(outcome.finalLoss) &&
                            outcome.finalLoss < outcome.initialLoss * 0.95f;
    outcome.requestedBackendObserved = tracker.requestedObserved();
    outcome.fallbackDetected = tracker.fallbackDetected();
    outcome.fallbackOperations = tracker.fallbackOperations();
    outcome.executedBackends = tracker.executedBackends();
    computeStatistics(outcome);

    if (outcome.status == "CANCELLED") {
        return outcome;
    }
    if (computationFailed) {
        outcome.status = "FAILED";
        return outcome;
    }
    if (outcome.completedSteps != totalSteps) {
        outcome.status = "FAILED";
        if (outcome.error.empty()) {
            outcome.error = "training ended before all requested steps completed";
        }
        return outcome;
    }
    if (!outcome.lossDecreased || !outcome.weightsChanged || outcome.nanDetected) {
        outcome.status = "FAILED";
        if (outcome.error.empty()) {
            outcome.error = "correctness condition failed (loss decrease, weight update, or finite loss)";
        }
        return outcome;
    }
    if (config.backend != BackendKind::CPU && outcome.fallbackDetected) {
        outcome.status = "FAILED";
        outcome.error = "CPU fallback detected for GPU run; see fallback_operations";
        return outcome;
    }

    outcome.status = "SUCCESS";
    outcome.error = "none";
    return outcome;
}

std::string mnnEnvironmentReport() {
    const bool cpuAvailable = MNN::MNNGetExtraRuntimeCreator(MNN_FORWARD_CPU) != nullptr;
    const bool openClAvailable = MNN::MNNGetExtraRuntimeCreator(MNN_FORWARD_OPENCL) != nullptr;
    const bool vulkanAvailable = MNN::MNNGetExtraRuntimeCreator(MNN_FORWARD_VULKAN) != nullptr;
    std::ostringstream stream;
    stream << "mnn_version=" << MNN::getVersion() << '\n'
           << "mnn_commit=" << kMnnCommit << '\n'
           << "compiled_backends=CPU,OPENCL,VULKAN\n"
           << "runtime_available_cpu=" << boolString(cpuAvailable) << '\n'
           << "runtime_available_opencl=" << boolString(openClAvailable) << '\n'
           << "runtime_available_vulkan=" << boolString(vulkanAvailable) << '\n'
           << "device_name=" << deviceName() << '\n'
           << "android_version=" << systemProperty("ro.build.version.release", "unknown") << '\n'
           << "android_api_level=" << android_get_device_api_level() << '\n'
           << "cpu_abi=" << cpuAbi() << '\n'
           << "optimizer=SGD\n"
           << "optimizer_host_sync=true";
    return stream.str();
}

}  // namespace phonelm
