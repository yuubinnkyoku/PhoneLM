#include "qnn_training_benchmark.h"
#include "qnn_runtime.h"
#include "../cpu_reference_training.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

namespace phonelm::qnn {
namespace {
using Clock = std::chrono::steady_clock;

double elapsedUs(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

struct Distribution {
    double minimum = 0.0;
    double median = 0.0;
    double mean = 0.0;
    double standardDeviation = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
};

Distribution summarize(std::vector<double> values) {
    Distribution result;
    if (values.empty()) return result;
    std::sort(values.begin(), values.end());
    result.minimum = values.front();
    result.maximum = values.back();
    result.mean = std::accumulate(values.begin(), values.end(), 0.0) /
                  static_cast<double>(values.size());
    double squared = 0.0;
    for (double value : values) {
        const double delta = value - result.mean;
        squared += delta * delta;
    }
    result.standardDeviation = std::sqrt(squared / static_cast<double>(values.size()));
    const auto percentile = [&](double fraction) {
        const auto index = static_cast<std::size_t>(
            std::ceil(static_cast<double>(values.size()) * fraction)) - 1;
        return values[std::min(index, values.size() - 1)];
    };
    result.median = values.size() % 2 == 0
        ? (values[values.size() / 2 - 1] + values[values.size() / 2]) / 2.0
        : values[values.size() / 2];
    result.p90 = percentile(0.90);
    result.p95 = percentile(0.95);
    return result;
}

void appendDistribution(std::ostringstream& stream, const char* name,
                        const Distribution& value) {
    stream << name << "_min_us=" << value.minimum << '\n'
           << name << "_median_us=" << value.median << '\n'
           << name << "_mean_us=" << value.mean << '\n'
           << name << "_stddev_us=" << value.standardDeviation << '\n'
           << name << "_p90_us=" << value.p90 << '\n'
           << name << "_p95_us=" << value.p95 << '\n'
           << name << "_max_us=" << value.maximum << '\n';
}

struct Dataset {
    cpu::Matrix input;
    cpu::Matrix target;
    cpu::Matrix trueWeight;
    cpu::Matrix initialWeight;
};

Dataset makeDataset(const TrainingConfig& config) {
    std::mt19937 generator(static_cast<std::mt19937::result_type>(config.seed));
    std::uniform_real_distribution<float> inputDistribution(-0.25f, 0.25f);
    std::uniform_real_distribution<float> trueWeightDistribution(-0.25f, 0.25f);
    std::uniform_real_distribution<float> initialWeightDistribution(-0.05f, 0.05f);
    Dataset dataset{
        cpu::Matrix(config.sampleCount, config.dimension),
        cpu::Matrix(config.sampleCount, config.dimension),
        cpu::Matrix(config.dimension, config.dimension),
        cpu::Matrix(config.dimension, config.dimension),
    };
    for (float& value : dataset.input.values) value = inputDistribution(generator);
    if (config.sampleCount >= config.dimension) {
        for (int row = 0; row < config.dimension; ++row) {
            for (int column = 0; column < config.dimension; ++column) {
                dataset.input.at(row, column) = row == column ? 0.25f : 0.0f;
            }
        }
    }
    for (float& value : dataset.trueWeight.values) value = trueWeightDistribution(generator);
    for (float& value : dataset.initialWeight.values) value = initialWeightDistribution(generator);
    dataset.target = cpu::matMul(dataset.input, dataset.trueWeight);
    return dataset;
}

double rmsDifference(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.size() != rhs.size() || lhs.empty()) return std::numeric_limits<double>::infinity();
    double squared = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        const double delta = static_cast<double>(lhs[index]) - rhs[index];
        squared += delta * delta;
    }
    return std::sqrt(squared / static_cast<double>(lhs.size()));
}

float maxDifference(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.size() != rhs.size()) return std::numeric_limits<float>::infinity();
    float maximum = 0.0f;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        maximum = std::max(maximum, std::fabs(lhs[index] - rhs[index]));
    }
    return maximum;
}

bool finite(const std::vector<float>& values) {
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}

struct StepRecord {
    int step = 0;
    int epoch = 0;
    int batchIndex = 0;
    bool measured = false;
    bool referenceChecked = false;
    double loss = 0.0;
    double weightError = 0.0;
    double inputPrepareUs = 0.0;
    double inputCopyUs = 0.0;
    double weightPrepareUs = 0.0;
    double preExecuteSyncUs = 0.0;
    double executeUs = 0.0;
    double postExecuteSyncUs = 0.0;
    double outputCopyUs = 0.0;
    double referenceCheckUs = 0.0;
    double lossUs = 0.0;
    double gradientUs = 0.0;
    double optimizerUs = 0.0;
    double weightBufferCopyUs = 0.0;
    double runtimeWeightUpdateUs = 0.0;
    double resultRecordingUs = 0.0;
    double fullStepUs = 0.0;
};

struct EpochRecord {
    int epoch = 0;
    double loss = 0.0;
    double weightError = 0.0;
};

std::string failure(ExecutionMode mode, const std::string& api, const std::string& error,
                    const std::string& diagnostics) {
    std::ostringstream stream;
    stream << "QNN_TRAINING_BENCHMARK_RESULT\nexecution_mode=" << executionModeName(mode)
           << "\nstatus=FAILED\nfailed_api=" << api
           << "\ncpu_fallback=false\nhtp_execute_failures="
           << (api == "graph_execute" ? 1 : 0)
           << "\nruntime_weight_update_failures="
           << (api == "runtime_weight_update" ? 1 : 0) << '\n'
           << diagnostics << "error=" << error;
    return stream.str();
}

void collect(const std::vector<StepRecord>& records,
             double StepRecord::* member, std::vector<double>& destination) {
    for (const auto& record : records) {
        if (record.measured) destination.push_back(record.*member);
    }
}

}  // namespace

std::string runTrainingBenchmarkExperiment(ExecutionMode mode,
                                           const TrainingConfig& requestedConfig) {
    const bool htp = mode == ExecutionMode::QNN_HTP_MULTIBATCH_TRAINING ||
                     mode == ExecutionMode::QNN_HTP_TRAINING_BENCHMARK;
    const bool benchmark = requestedConfig.benchmarkMode ||
                           mode == ExecutionMode::QNN_CPU_TRAINING_BENCHMARK ||
                           mode == ExecutionMode::QNN_HTP_TRAINING_BENCHMARK;
    TrainingConfig config = requestedConfig;
    if (config.sampleCount < config.batchSize || config.sampleCount < config.dimension) {
        std::ostringstream error;
        error << "sample_count must be >= max(batch_size,input_dim); sample_count="
              << config.sampleCount;
        return failure(mode, "config_validation", error.str(), {});
    }
    const int batchesPerEpoch =
        (config.sampleCount + config.batchSize - 1) / config.batchSize;
    const int trainingSteps = config.epochs > 0
        ? config.epochs * batchesPerEpoch
        : config.steps;
    const int warmupSteps = benchmark ? config.warmupSteps : 0;
    const int requestedMeasured = config.measuredSteps > 0
        ? config.measuredSteps
        : std::max(0, trainingSteps - warmupSteps);
    const int totalSteps = benchmark
        ? warmupSteps + requestedMeasured
        : trainingSteps;
    if (totalSteps <= 0) {
        return failure(mode, "config_validation", "total step count must be positive", {});
    }
    const int correctnessInterval = config.correctnessInterval > 0
        ? config.correctnessInterval
        : (benchmark ? std::max(1, requestedMeasured / 5) : 1);

    const auto totalStarted = Clock::now();
    const Dataset dataset = makeDataset(config);
    cpu::Matrix weight = dataset.initialWeight;
    cpu::Matrix referenceWeight = dataset.initialWeight;
    const double initialWeightError =
        rmsDifference(weight.values, dataset.trueWeight.values);
    const auto initialPrediction = cpu::matMul(dataset.input, weight);
    const float initialLoss = cpu::meanSquaredError(initialPrediction, dataset.target);

    Runtime runtime;
    std::string error;
    double initializationUs = 0.0;
    if (htp) {
        const auto initializationStarted = Clock::now();
        if (!runtime.initialize(QnnBackendKind::HTP, error)) {
            return failure(mode, "runtime_initialize", error, runtime.diagnostics());
        }
        if (!runtime.prepareMatMul(config.batchSize, config.dimension,
                                   config.dimension, false, error)) {
            return failure(mode, "forward_graph_prepare", error, runtime.diagnostics());
        }
        if (!runtime.setInitialWeight(weight.values, error)) {
            return failure(mode, "initial_weight_bind", error, runtime.diagnostics());
        }
        initializationUs = elapsedUs(initializationStarted);
    }

    std::vector<std::size_t> order(static_cast<std::size_t>(config.sampleCount));
    std::iota(order.begin(), order.end(), 0);
    std::mt19937 shuffleGenerator(
        static_cast<std::mt19937::result_type>(config.seed ^ 0x9e3779b9U));
    std::vector<StepRecord> records;
    records.reserve(static_cast<std::size_t>(totalSteps));
    std::vector<EpochRecord> epochs;
    std::vector<float> prediction;
    cpu::Matrix lastInput(config.batchSize, config.dimension);
    cpu::Matrix lastTarget(config.batchSize, config.dimension);
    float maximumForwardError = 0.0f;
    bool nonFinite = false;
    bool referenceMatched = true;

    for (int step = 0; step < totalSteps; ++step) {
        StepRecord record;
        record.step = step;
        record.epoch = step / batchesPerEpoch;
        record.batchIndex = step % batchesPerEpoch;
        record.measured = step >= warmupSteps;
        if (record.batchIndex == 0) {
            std::shuffle(order.begin(), order.end(), shuffleGenerator);
        }

        const auto fullStepStarted = Clock::now();
        auto section = Clock::now();
        const std::size_t first =
            static_cast<std::size_t>(record.batchIndex * config.batchSize);
        std::vector<std::size_t> batchRows(static_cast<std::size_t>(config.batchSize));
        for (int row = 0; row < config.batchSize; ++row) {
            batchRows[static_cast<std::size_t>(row)] =
                order[(first + static_cast<std::size_t>(row)) % order.size()];
        }
        record.inputPrepareUs = elapsedUs(section);

        section = Clock::now();
        for (int row = 0; row < config.batchSize; ++row) {
            const std::size_t sourceRow = batchRows[static_cast<std::size_t>(row)];
            const auto inputSource = dataset.input.values.begin() +
                static_cast<std::ptrdiff_t>(sourceRow * config.dimension);
            const auto targetSource = dataset.target.values.begin() +
                static_cast<std::ptrdiff_t>(sourceRow * config.dimension);
            std::copy_n(inputSource, config.dimension,
                        lastInput.values.begin() +
                        static_cast<std::ptrdiff_t>(row * config.dimension));
            std::copy_n(targetSource, config.dimension,
                        lastTarget.values.begin() +
                        static_cast<std::ptrdiff_t>(row * config.dimension));
        }
        record.inputCopyUs = elapsedUs(section);

        cpu::Matrix predicted(config.batchSize, config.dimension);
        if (htp) {
            if (!runtime.executePrepared(lastInput.values, prediction, error)) {
                return failure(mode, "graph_execute", error, runtime.diagnostics());
            }
            record.executeUs = runtime.metrics().executeUs.back();
            record.inputCopyUs += runtime.metrics().inputBindUs.back();
            section = Clock::now();
            std::copy(prediction.begin(), prediction.end(), predicted.values.begin());
            record.outputCopyUs = elapsedUs(section) + runtime.metrics().outputBindUs.back();
        } else {
            section = Clock::now();
            predicted = cpu::matMul(lastInput, weight);
            record.executeUs = elapsedUs(section);
            prediction = predicted.values;
        }

        section = Clock::now();
        const float loss = cpu::meanSquaredError(predicted, lastTarget);
        record.lossUs = elapsedUs(section);
        record.loss = loss;

        section = Clock::now();
        const auto difference = cpu::subtract(predicted, lastTarget);
        cpu::Matrix dPrediction(config.batchSize, config.dimension);
        const float scale =
            2.0f / static_cast<float>(config.batchSize * config.dimension);
        for (std::size_t index = 0; index < difference.values.size(); ++index) {
            dPrediction.values[index] = difference.values[index] * scale;
        }
        const auto gradient = cpu::matMul(cpu::transpose(lastInput), dPrediction);
        record.gradientUs = elapsedUs(section);

        const double preReferenceStepUs = elapsedUs(fullStepStarted);
        const bool checkReference =
            !benchmark || step == 0 || step == totalSteps - 1 ||
            ((step - warmupSteps) >= 0 &&
             (step - warmupSteps) % correctnessInterval == 0);
        if (checkReference) {
            section = Clock::now();
            const auto expected = cpu::matMul(lastInput, weight);
            const float forwardError = maxDifference(prediction, expected.values);
            maximumForwardError = std::max(maximumForwardError, forwardError);
            referenceMatched &= !htp || forwardError <= 1.0e-3f;
            if (!benchmark) {
                const auto state =
                    cpu::forwardBackward(lastInput, lastTarget, referenceWeight);
                cpu::sgdUpdate(referenceWeight, state.dWeight, config.learningRate);
            }
            record.referenceCheckUs = elapsedUs(section);
            record.referenceChecked = true;
        }

        const auto postReferenceStarted = Clock::now();
        section = Clock::now();
        cpu::sgdUpdate(weight, gradient, config.learningRate);
        record.optimizerUs = elapsedUs(section);
        record.weightError = rmsDifference(weight.values, dataset.trueWeight.values);

        if (htp) {
            if (!runtime.updateWeight(weight.values, error)) {
                return failure(mode, "runtime_weight_update", error, runtime.diagnostics());
            }
            record.weightBufferCopyUs = runtime.metrics().weightBufferCopyUs.back();
            record.runtimeWeightUpdateUs = runtime.metrics().weightUpdateUs.back();
        }
        record.fullStepUs = preReferenceStepUs + elapsedUs(postReferenceStarted);

        nonFinite |= !std::isfinite(loss) || !finite(weight.values) ||
                     !finite(gradient.values) || !finite(prediction);
        section = Clock::now();
        records.push_back(record);
        records.back().resultRecordingUs = elapsedUs(section);
        if (nonFinite) break;

        if ((step + 1) % batchesPerEpoch == 0 || step + 1 == totalSteps) {
            const auto fullPrediction = cpu::matMul(dataset.input, weight);
            epochs.push_back({
                record.epoch,
                cpu::meanSquaredError(fullPrediction, dataset.target),
                rmsDifference(weight.values, dataset.trueWeight.values),
            });
        }
    }

    float finalVerificationError = 0.0f;
    if (htp && !nonFinite) {
        if (!runtime.executePrepared(lastInput.values, prediction, error)) {
            return failure(mode, "final_weight_verification_execute", error,
                           runtime.diagnostics());
        }
        const auto expected = cpu::matMul(lastInput, weight);
        finalVerificationError = maxDifference(prediction, expected.values);
        maximumForwardError =
            std::max(maximumForwardError, finalVerificationError);
        referenceMatched &= finalVerificationError <= 1.0e-3f;
    }

    const auto finalPrediction = cpu::matMul(dataset.input, weight);
    const float finalLoss = cpu::meanSquaredError(finalPrediction, dataset.target);
    const double finalWeightError =
        rmsDifference(weight.values, dataset.trueWeight.values);
    const float finalWeightDifference = benchmark
        ? 0.0f
        : maxDifference(weight.values, referenceWeight.values);
    const bool completed = records.size() == static_cast<std::size_t>(totalSteps);
    const bool success = completed && !nonFinite && std::isfinite(finalLoss) &&
                         finalLoss < initialLoss &&
                         finalWeightError < initialWeightError &&
                         referenceMatched &&
                         (!htp || runtime.metrics().runtimeWeightUpdateCount ==
                                      static_cast<std::uint64_t>(totalSteps));

    std::vector<double> inputPrepare, inputCopy, weightPrepare, preSync, execute,
        postSync, outputCopy, referenceCheck, loss, gradient, optimizer,
        weightCopy, runtimeUpdate, resultRecording, fullStep;
    collect(records, &StepRecord::inputPrepareUs, inputPrepare);
    collect(records, &StepRecord::inputCopyUs, inputCopy);
    collect(records, &StepRecord::weightPrepareUs, weightPrepare);
    collect(records, &StepRecord::preExecuteSyncUs, preSync);
    collect(records, &StepRecord::executeUs, execute);
    if (htp && warmupSteps == 0 && !execute.empty()) {
        execute.erase(execute.begin());
    }
    collect(records, &StepRecord::postExecuteSyncUs, postSync);
    collect(records, &StepRecord::outputCopyUs, outputCopy);
    collect(records, &StepRecord::referenceCheckUs, referenceCheck);
    collect(records, &StepRecord::lossUs, loss);
    collect(records, &StepRecord::gradientUs, gradient);
    collect(records, &StepRecord::optimizerUs, optimizer);
    collect(records, &StepRecord::weightBufferCopyUs, weightCopy);
    collect(records, &StepRecord::runtimeWeightUpdateUs, runtimeUpdate);
    collect(records, &StepRecord::resultRecordingUs, resultRecording);
    collect(records, &StepRecord::fullStepUs, fullStep);

    std::ostringstream stream;
    stream << std::setprecision(9)
           << "QNN_TRAINING_BENCHMARK_RESULT\n"
           << "execution_mode=" << executionModeName(mode) << '\n'
           << "status=" << (success ? "SUCCESS" : "FAILED") << '\n'
           << "measurement_mode=" << (benchmark ? "benchmark" : "correctness") << '\n'
           << "backend=" << (htp ? "HTP" : "CPU") << '\n'
           << "backend_library=" << (htp ? "libQnnHtp.so" : "none") << '\n';
    if (htp) stream << runtime.diagnostics();
    stream << "seed=" << config.seed << '\n'
           << "sample_count=" << config.sampleCount << '\n'
           << "batch_size=" << config.batchSize << '\n'
           << "input_dim=" << config.dimension << '\n'
           << "output_dim=" << config.dimension << '\n'
           << "dataset_shape=" << config.sampleCount << 'x' << config.dimension << '\n'
           << "dataset_rank=" << (config.sampleCount >= config.dimension
                                      ? config.dimension : config.sampleCount) << '\n'
           << "dataset_rank_basis=embedded_scaled_identity_rows\n"
           << "shuffle=mt19937_per_epoch\n"
           << "epochs=" << (config.epochs > 0 ? config.epochs :
                              (totalSteps + batchesPerEpoch - 1) / batchesPerEpoch) << '\n'
           << "steps=" << totalSteps << '\n'
           << "warmup_steps=" << warmupSteps << '\n'
           << "measured_steps=" << fullStep.size() << '\n'
           << "correctness_interval=" << correctnessInterval << '\n'
           << "learning_rate=" << config.learningRate << '\n'
           << "initial_loss=" << initialLoss << '\n'
           << "final_loss=" << finalLoss << '\n'
           << "loss_reduction_ratio=" << finalLoss / initialLoss << '\n'
           << "initial_weight_error=" << initialWeightError << '\n'
           << "final_weight_error=" << finalWeightError << '\n'
           << "weight_error_reduction_ratio="
           << finalWeightError / initialWeightError << '\n'
           << "final_weight_difference=" << finalWeightDifference << '\n'
           << "final_verification_max_abs_error=" << finalVerificationError << '\n'
           << "max_abs_error=" << maximumForwardError << '\n'
           << "nan_detected=" << (nonFinite ? "true" : "false") << '\n'
           << "inf_detected=" << (nonFinite ? "true" : "false") << '\n'
           << "cpu_fallback=false\n"
           << "htp_execute_failures=0\n"
           << "runtime_weight_update_failures=0\n"
           << "npu_forward_used=" << (htp ? "true" : "false") << '\n'
           << "cpu_operations="
           << (htp ? "loss,gradient,sgd_update,app_write_weight_copy"
                   : "forward,loss,gradient,sgd_update") << '\n'
           << "npu_operations=" << (htp ? "forward" : "none") << '\n'
           << "backward_on_htp=false\noptimizer_on_htp=false\n"
           << "graph_create_count=" << (htp ? runtime.metrics().graphCreateCount : 0) << '\n'
           << "graph_finalize_count=" << (htp ? runtime.metrics().graphFinalizeCount : 0) << '\n'
           << "graph_execute_count=" << (htp ? runtime.metrics().graphExecuteCount : 0) << '\n'
           << "runtime_weight_update_count="
           << (htp ? runtime.metrics().runtimeWeightUpdateCount : 0) << '\n'
           << "graph_reused=" << (htp ? "true" : "not_applicable") << '\n'
           << "qnn_tensor_memory=RAW_app_owned_client_buffers\n"
           << "explicit_pre_execute_sync=false\n"
           << "explicit_post_execute_sync=false\n"
           << "qnn_execute_call_includes_completion_wait=true\n"
           << "initialization_us=" << initializationUs << '\n'
           << "backend_create_time_us=" << (htp ? runtime.metrics().backendCreateUs : 0.0) << '\n'
           << "device_create_time_us=" << (htp ? runtime.metrics().deviceCreateUs : 0.0) << '\n'
           << "context_create_time_us=" << (htp ? runtime.metrics().contextCreateUs : 0.0) << '\n'
           << "graph_create_time_us=" << (htp ? runtime.metrics().graphCreateUs : 0.0) << '\n'
           << "graph_finalize_time_us=" << (htp ? runtime.metrics().graphFinalizeUs : 0.0) << '\n'
           << "first_execute_time_us="
           << (htp && !runtime.metrics().executeUs.empty()
                   ? runtime.metrics().executeUs.front() : 0.0) << '\n';
    appendDistribution(stream, "input_prepare", summarize(inputPrepare));
    appendDistribution(stream, "input_copy", summarize(inputCopy));
    appendDistribution(stream, "weight_prepare", summarize(weightPrepare));
    appendDistribution(stream, "pre_execute_sync", summarize(preSync));
    appendDistribution(stream, "steady_state_execute", summarize(execute));
    appendDistribution(stream, "post_execute_sync", summarize(postSync));
    appendDistribution(stream, "output_copy", summarize(outputCopy));
    appendDistribution(stream, "cpu_reference_check", summarize(referenceCheck));
    appendDistribution(stream, "cpu_loss", summarize(loss));
    appendDistribution(stream, "cpu_gradient", summarize(gradient));
    appendDistribution(stream, "cpu_optimizer", summarize(optimizer));
    appendDistribution(stream, "weight_buffer_copy", summarize(weightCopy));
    appendDistribution(stream, "runtime_weight_update", summarize(runtimeUpdate));
    appendDistribution(stream, "result_recording", summarize(resultRecording));
    appendDistribution(stream, "full_step", summarize(fullStep));
    stream << "total_training_time_us=" << elapsedUs(totalStarted) << '\n'
           << "timings_csv_begin\n"
           << "step,epoch,batch_index,measured,reference_checked,input_prepare_us,input_copy_us,weight_prepare_us,pre_execute_sync_us,qnn_execute_call_us,post_execute_sync_us,output_copy_us,cpu_reference_check_us,cpu_loss_us,cpu_gradient_us,cpu_optimizer_us,weight_buffer_copy_us,runtime_weight_update_us,result_recording_us,full_step_us,loss,weight_error\n";
    for (const auto& record : records) {
        stream << record.step << ',' << record.epoch << ',' << record.batchIndex << ','
               << (record.measured ? "true" : "false") << ','
               << (record.referenceChecked ? "true" : "false") << ','
               << record.inputPrepareUs << ',' << record.inputCopyUs << ','
               << record.weightPrepareUs << ',' << record.preExecuteSyncUs << ','
               << record.executeUs << ',' << record.postExecuteSyncUs << ','
               << record.outputCopyUs << ',' << record.referenceCheckUs << ','
               << record.lossUs << ',' << record.gradientUs << ','
               << record.optimizerUs << ',' << record.weightBufferCopyUs << ','
               << record.runtimeWeightUpdateUs << ',' << record.resultRecordingUs << ','
               << record.fullStepUs << ',' << record.loss << ','
               << record.weightError << '\n';
    }
    stream << "timings_csv_end\nepoch_csv_begin\n"
           << "epoch,dataset_loss,weight_error\n";
    for (const auto& epoch : epochs) {
        stream << epoch.epoch << ',' << epoch.loss << ',' << epoch.weightError << '\n';
    }
    stream << "epoch_csv_end\nerror="
           << (success ? "none" : "training correctness condition failed");
    return stream.str();
}

}  // namespace phonelm::qnn
