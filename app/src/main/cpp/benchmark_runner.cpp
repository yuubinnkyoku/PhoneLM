#include "benchmark_runner.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace phonelm {
namespace {

std::string numberOrNaN(float value) {
    if (!std::isfinite(value)) {
        return "nan";
    }
    std::ostringstream stream;
    stream << std::setprecision(9) << value;
    return stream.str();
}

std::string fixedNumber(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

std::string sanitizeValue(std::string value) {
    for (char& character : value) {
        if (character == '\n' || character == '\r') {
            character = ' ';
        }
    }
    return value;
}

std::string formatResult(const TrainingConfig& config, const TrainingOutcome& result) {
    std::ostringstream stream;
    stream << "RESULT\n"
           << "backend_requested=" << result.backendRequested << '\n'
           << "backend_actual=" << result.backendActual << '\n'
           << "executed_backends=" << result.executedBackends << '\n'
           << "batch_size=" << config.batchSize << '\n'
           << "dimension=" << config.dimension << '\n'
           << "steps=" << config.steps << '\n'
           << "warmup_steps=" << config.warmupSteps << '\n'
           << "completed_steps=" << result.completedSteps << '\n'
           << "initial_loss=" << numberOrNaN(result.initialLoss) << '\n'
           << "final_loss=" << numberOrNaN(result.finalLoss) << '\n'
           << "average_step_time_ms=" << fixedNumber(result.averageStepTimeMs) << '\n'
           << "median_step_time_ms=" << fixedNumber(result.medianStepTimeMs) << '\n'
           << "p95_step_time_ms=" << fixedNumber(result.p95StepTimeMs) << '\n'
           << "total_time_ms=" << fixedNumber(result.totalTimeMs) << '\n'
           << "loss_decreased=" << (result.lossDecreased ? "true" : "false") << '\n'
           << "weights_changed=" << (result.weightsChanged ? "true" : "false") << '\n'
           << "nan_detected=" << (result.nanDetected ? "true" : "false") << '\n'
           << "fallback_detected=" << (result.fallbackDetected ? "true" : "false") << '\n'
           << "fallback_operations=" << sanitizeValue(result.fallbackOperations) << '\n'
           << "optimizer_host_sync=" << (result.optimizerHostSync ? "true" : "false") << '\n'
           << "status=" << result.status << '\n'
           << "error=" << sanitizeValue(result.error);
    return stream.str();
}

}  // namespace

std::string BenchmarkRunner::run(const TrainingConfig& config,
                                 std::atomic_bool& stopRequested,
                                 const LogSink& log) {
    std::string validationError;
    if (!validateTrainingConfig(config, validationError)) {
        TrainingOutcome invalid;
        invalid.backendRequested = backendName(config.backend);
        invalid.error = validationError;
        const auto report = formatResult(config, invalid);
        if (log) {
            log(report);
        }
        return report;
    }

    auto outcome = runMnnTraining(config, stopRequested, log);
    const auto report = formatResult(config, outcome);
    if (log) {
        log(report);
    }
    return report;
}

std::string BenchmarkRunner::environmentReport() {
    return mnnEnvironmentReport();
}

}  // namespace phonelm
