#include "qnn_linear_training.h"
#include "qnn_training_benchmark.h"
#include "qnn_runtime.h"
#include "../cpu_reference_training.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>

namespace phonelm::qnn {
namespace {
using Clock = std::chrono::steady_clock;

struct ErrorStats {
    double sum = 0.0;
    float maximum = 0.0f;
    float maxRelative = 0.0f;
    std::size_t count = 0;
};
struct StepRecord {
    int step = 0;
    double loss = 0.0;
    double cpuReferenceLoss = 0.0;
    double weightError = 0.0;
    double gradientNorm = 0.0;
    double updateNorm = 0.0;
    double forwardUs = 0.0;
    double lossUs = 0.0;
    double gradientUs = 0.0;
    double optimizerUs = 0.0;
    double runtimeWeightUpdateUs = 0.0;
    double totalUs = 0.0;
};
struct Distribution {
    double minimum = 0.0;
    double median = 0.0;
    double mean = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
};
struct Problem {
    cpu::Matrix input;
    cpu::Matrix trueWeight;
    cpu::Matrix initialWeight;
    cpu::Matrix target;
};

inline double elapsedUs(Clock::time_point start) {
    return std::chrono::duration<double, std::micro>(Clock::now() - start).count();
}

void compare(const std::vector<float>& actual, const std::vector<float>& expected,
             ErrorStats& stats) {
    if (actual.size() != expected.size()) {
        stats.maximum = std::numeric_limits<float>::infinity();
        return;
    }
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const float error = std::fabs(actual[i] - expected[i]);
        const float denominator = std::max(1.0e-7f, std::fabs(actual[i]) + std::fabs(expected[i]));
        stats.sum += error;
        stats.maximum = std::max(stats.maximum, error);
        stats.maxRelative = std::max(stats.maxRelative, error / denominator);
        ++stats.count;
    }
}

double l2Norm(const std::vector<float>& values) {
    double sum = 0.0;
    for (float value : values) sum += static_cast<double>(value) * value;
    return std::sqrt(sum);
}

double rmsDifference(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.size() != rhs.size() || lhs.empty()) return std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        const double difference = static_cast<double>(lhs[i]) - rhs[i];
        sum += difference * difference;
    }
    return std::sqrt(sum / static_cast<double>(lhs.size()));
}

float maxDifference(const std::vector<float>& lhs, const std::vector<float>& rhs) {
    if (lhs.size() != rhs.size()) return std::numeric_limits<float>::infinity();
    float maximum = 0.0f;
    for (std::size_t i = 0; i < lhs.size(); ++i) maximum = std::max(maximum, std::fabs(lhs[i] - rhs[i]));
    return maximum;
}

bool allFinite(const std::vector<float>& values) {
    return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
}

Distribution distribution(std::vector<double> values) {
    Distribution result;
    if (values.empty()) return result;
    std::sort(values.begin(), values.end());
    result.minimum = values.front();
    result.maximum = values.back();
    result.mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    const std::size_t middle = values.size() / 2;
    result.median = values.size() % 2 == 0 ? (values[middle - 1] + values[middle]) / 2.0 : values[middle];
    result.p95 = values[static_cast<std::size_t>(std::ceil(values.size() * 0.95)) - 1];
    return result;
}

Problem makeProblem(const TrainingConfig& config) {
    std::mt19937 generator(static_cast<std::mt19937::result_type>(config.seed));
    std::uniform_real_distribution<float> inputDistribution(-0.25f, 0.25f);
    std::uniform_real_distribution<float> trueWeightDistribution(-0.25f, 0.25f);
    std::uniform_real_distribution<float> initialWeightDistribution(-0.05f, 0.05f);
    Problem problem{cpu::Matrix(config.batchSize, config.dimension),
                    cpu::Matrix(config.dimension, config.dimension),
                    cpu::Matrix(config.dimension, config.dimension),
                    cpu::Matrix(config.batchSize, config.dimension)};
    for (float& value : problem.input.values) value = inputDistribution(generator);
    for (float& value : problem.trueWeight.values) value = trueWeightDistribution(generator);
    for (float& value : problem.initialWeight.values) value = initialWeightDistribution(generator);
    problem.target = cpu::matMul(problem.input, problem.trueWeight);
    return problem;
}

std::string failure(ExecutionMode mode, const char* backend, const std::string& api,
                    const std::string& error, const std::string& diagnostics = {}) {
    std::ostringstream stream;
    stream << "QNN_EXPERIMENT_RESULT\nexecution_mode=" << executionModeName(mode)
           << "\nqnn_backend=" << backend
           << "\nbackend_library=" << (std::string(backend) == "HTP" ? "libQnnHtp.so" : "none")
           << '\n' << diagnostics
           << "failed_api=" << api << "\ncpu_fallback=false\nnpu_forward_used=false\n"
           << "status=FAILED\nerror=" << error;
    return stream.str();
}

void appendDistribution(std::ostringstream& stream, const char* prefix, const Distribution& value) {
    stream << prefix << "_min_us=" << value.minimum << '\n'
           << prefix << "_median_us=" << value.median << '\n'
           << prefix << "_mean_us=" << value.mean << '\n'
           << prefix << "_p95_us=" << value.p95 << '\n'
           << prefix << "_max_us=" << value.maximum << '\n';
}

std::string gradientCheckReport(const TrainingConfig& config) {
    const auto started = Clock::now();
    const auto check = cpu::gradientCheck(2, 4, 1.0e-3f);
    std::ostringstream stream;
    stream << std::setprecision(9)
           << "QNN_LINEAR_GRADIENT_CHECK_RESULT\nexecution_mode=QNN_LINEAR_GRADIENT_CHECK\n"
           << "status=" << (check.passed ? "SUCCESS" : "FAILED") << '\n'
           << "backend_library=none\nbatch_size=2\ninput_dim=4\noutput_dim=4\nsteps=0\n"
           << "learning_rate=" << config.learningRate << '\n'
           << "gradient_check_max_abs_dw=" << check.maxAbsoluteErrorWeight << '\n'
           << "gradient_check_max_rel_dw=" << check.maxRelativeErrorWeight << '\n'
           << "gradient_check_max_abs_dx=" << check.maxAbsoluteErrorInput << '\n'
           << "gradient_check_max_rel_dx=" << check.maxRelativeErrorInput << '\n'
           << "gradient_check_passed=" << (check.passed ? "true" : "false") << '\n'
           << "nan_detected=false\ninf_detected=false\nnpu_forward_used=false\ncpu_fallback=false\n"
           << "total_time_us=" << elapsedUs(started) << '\n'
           << "error=" << (check.passed ? "none" : "finite difference gradient check failed");
    return stream.str();
}

std::string trainingReport(ExecutionMode mode, const TrainingConfig& config, bool htp) {
    const auto totalStarted = Clock::now();
    const Problem problem = makeProblem(config);
    cpu::Matrix weight = problem.initialWeight;
    cpu::Matrix cpuReferenceWeight = problem.initialWeight;
    const double initialWeightError = rmsDifference(weight.values, problem.trueWeight.values);
    std::vector<StepRecord> records;
    records.reserve(config.steps);
    ErrorStats forwardError;
    std::vector<float> previousPrediction;
    bool everyUpdatedOutputChanged = true;
    bool everyOutputMatchedUpdatedReference = true;
    bool nonFinite = false;
    std::string error;
    Runtime runtime;

    if (htp) {
        if (!runtime.initialize(QnnBackendKind::HTP, error)) {
            return failure(mode, "HTP", "runtime_initialize", error, runtime.diagnostics());
        }
        if (!runtime.prepareMatMul(config.batchSize, config.dimension, config.dimension, false, error)) {
            return failure(mode, "HTP", "forward_graph_prepare", error, runtime.diagnostics());
        }
        if (!runtime.setInitialWeight(weight.values, error)) {
            return failure(mode, "HTP", "initial_weight_bind", error, runtime.diagnostics());
        }
    }

    float initialLoss = 0.0f;
    std::vector<float> prediction;
    std::vector<float> stepZeroWeight = weight.values;
    std::vector<float> stepZeroOutput;
    std::vector<float> postStepZeroWeight;
    std::vector<float> stepOneOutput;

    for (int step = 0; step < config.steps; ++step) {
        const auto stepStarted = Clock::now();
        StepRecord record;
        record.step = step;
        cpu::Matrix predicted(config.batchSize, config.dimension);
        const auto forwardStarted = Clock::now();
        if (htp) {
            if (!runtime.executePrepared(problem.input.values, prediction, error)) {
                return failure(mode, "HTP", "graph_execute", error, runtime.diagnostics());
            }
            predicted.values = prediction;
            record.forwardUs = runtime.metrics().executeUs.back();
            const auto cpuPrediction = cpu::matMul(problem.input, weight);
            compare(prediction, cpuPrediction.values, forwardError);
            everyOutputMatchedUpdatedReference &= maxDifference(prediction, cpuPrediction.values) <= 1.0e-3f;
            if (!previousPrediction.empty()) {
                everyUpdatedOutputChanged &= maxDifference(previousPrediction, prediction) > 0.0f;
            }
            previousPrediction = prediction;
            if (step == 0) stepZeroOutput = prediction;
            if (step == 1) stepOneOutput = prediction;
        } else {
            predicted = cpu::matMul(problem.input, weight);
            prediction = predicted.values;
            record.forwardUs = elapsedUs(forwardStarted);
        }

        const auto lossStarted = Clock::now();
        const float loss = cpu::meanSquaredError(predicted, problem.target);
        record.lossUs = elapsedUs(lossStarted);
        record.loss = loss;
        if (step == 0) initialLoss = loss;

        const auto gradientStarted = Clock::now();
        const auto difference = cpu::subtract(predicted, problem.target);
        cpu::Matrix dPrediction(config.batchSize, config.dimension);
        const float scale = 2.0f / static_cast<float>(config.batchSize * config.dimension);
        for (std::size_t i = 0; i < difference.values.size(); ++i) {
            dPrediction.values[i] = difference.values[i] * scale;
        }
        const auto gradient = cpu::matMul(cpu::transpose(problem.input), dPrediction);
        record.gradientUs = elapsedUs(gradientStarted);
        record.gradientNorm = l2Norm(gradient.values);
        record.updateNorm = config.learningRate * record.gradientNorm;

        const auto cpuReferenceState = cpu::forwardBackward(problem.input, problem.target, cpuReferenceWeight);
        record.cpuReferenceLoss = cpuReferenceState.loss;
        cpu::sgdUpdate(cpuReferenceWeight, cpuReferenceState.dWeight, config.learningRate);

        const auto optimizerStarted = Clock::now();
        cpu::sgdUpdate(weight, gradient, config.learningRate);
        record.optimizerUs = elapsedUs(optimizerStarted);
        record.weightError = rmsDifference(weight.values, problem.trueWeight.values);
        if (step == 0) postStepZeroWeight = weight.values;

        if (htp) {
            if (!runtime.updateWeight(weight.values, error)) {
                return failure(mode, "HTP", "runtime_weight_update", error, runtime.diagnostics());
            }
            record.runtimeWeightUpdateUs = runtime.metrics().weightUpdateUs.back();
        }
        nonFinite |= !std::isfinite(loss) || !allFinite(weight.values) || !allFinite(gradient.values);
        record.totalUs = elapsedUs(stepStarted);
        records.push_back(record);
        if (nonFinite) break;
    }

    cpu::Matrix finalPrediction(config.batchSize, config.dimension);
    if (htp && !nonFinite) {
        if (!runtime.executePrepared(problem.input.values, prediction, error)) {
            return failure(mode, "HTP", "final_weight_verification_execute", error, runtime.diagnostics());
        }
        finalPrediction.values = prediction;
        const auto expected = cpu::matMul(problem.input, weight);
        compare(prediction, expected.values, forwardError);
        everyOutputMatchedUpdatedReference &= maxDifference(prediction, expected.values) <= 1.0e-3f;
        everyUpdatedOutputChanged &= previousPrediction.empty() || maxDifference(previousPrediction, prediction) > 0.0f;
    } else if (!nonFinite) {
        finalPrediction = cpu::matMul(problem.input, weight);
    }

    const float finalLoss = nonFinite ? std::numeric_limits<float>::quiet_NaN()
                                      : cpu::meanSquaredError(finalPrediction, problem.target);
    const double finalWeightError = rmsDifference(weight.values, problem.trueWeight.values);
    const auto cpuFinalPrediction = cpu::matMul(problem.input, cpuReferenceWeight);
    const float cpuReferenceFinalLoss = cpu::meanSquaredError(cpuFinalPrediction, problem.target);
    const float finalWeightDifference = maxDifference(weight.values, cpuReferenceWeight.values);
    const float finalPredictionDifference = maxDifference(finalPrediction.values, cpuFinalPrediction.values);
    const bool weightChanged = maxDifference(problem.initialWeight.values, weight.values) > 0.0f;
    const bool outputReflection = !htp || (everyUpdatedOutputChanged && everyOutputMatchedUpdatedReference &&
        runtime.metrics().runtimeWeightUpdateCount == static_cast<std::uint64_t>(config.steps));
    const bool success = records.size() == static_cast<std::size_t>(config.steps) &&
        std::isfinite(finalLoss) && finalLoss < initialLoss && finalWeightError < initialWeightError &&
        weightChanged && !nonFinite && outputReflection && (!htp || forwardError.maximum <= 1.0e-3f);

    std::vector<double> fullStepTimes, lossTimes, gradientTimes, optimizerTimes;
    for (const auto& record : records) {
        fullStepTimes.push_back(record.totalUs);
        lossTimes.push_back(record.lossUs);
        gradientTimes.push_back(record.gradientUs);
        optimizerTimes.push_back(record.optimizerUs);
    }
    std::vector<double> steadyExecuteTimes;
    if (htp && runtime.metrics().executeUs.size() > 1) {
        steadyExecuteTimes.assign(runtime.metrics().executeUs.begin() + 1, runtime.metrics().executeUs.end());
    }
    const auto fullStepStats = distribution(fullStepTimes);
    const auto executeStats = distribution(steadyExecuteTimes);
    const auto weightUpdateStats = distribution(htp ? runtime.metrics().weightUpdateUs : std::vector<double>{});

    const float stepOutputDifference = stepZeroOutput.empty() || stepOneOutput.empty()
        ? 0.0f : maxDifference(stepZeroOutput, stepOneOutput);
    const float stepWeightDifference = postStepZeroWeight.empty()
        ? 0.0f : maxDifference(stepZeroWeight, postStepZeroWeight);

    std::ostringstream stream;
    stream << std::setprecision(9)
           << "QNN_LINEAR_TRAINING_RESULT\nexecution_mode=" << executionModeName(mode) << '\n'
           << "status=" << (success ? "SUCCESS" : "FAILED") << '\n'
           << "backend_library=" << (htp ? "libQnnHtp.so" : "none") << '\n';
    if (htp) stream << runtime.diagnostics();
    stream << "batch_size=" << config.batchSize << "\ninput_dim=" << config.dimension
           << "\noutput_dim=" << config.dimension << "\nsteps=" << config.steps
           << "\nlearning_rate=" << config.learningRate << "\nseed=" << config.seed << '\n'
           << "initial_loss=" << initialLoss << "\nfinal_loss=" << finalLoss
           << "\ncpu_reference_final_loss=" << cpuReferenceFinalLoss
           << "\nloss_ratio=" << finalLoss / initialLoss
           << "\ninitial_weight_error=" << initialWeightError
           << "\nfinal_weight_error=" << finalWeightError
           << "\nfinal_weight_difference=" << finalWeightDifference
           << "\nfinal_prediction_difference=" << finalPredictionDifference << '\n'
           << "graph_create_count=" << (htp ? runtime.metrics().graphCreateCount : 0)
           << "\ngraph_finalize_count=" << (htp ? runtime.metrics().graphFinalizeCount : 0)
           << "\ngraph_execute_count=" << (htp ? runtime.metrics().graphExecuteCount : 0)
           << "\nruntime_weight_update_count=" << (htp ? runtime.metrics().runtimeWeightUpdateCount : 0) << '\n'
           << "final_verification_execute_count=" << (htp ? 1 : 0)
           << "\ngraph_reused=" << (htp ? "true" : "not_applicable")
           << "\nruntime_weight_update_result=" << (htp ? "success" : "not_applicable")
           << "\nruntime_weight_update_worked=" << (htp ? (outputReflection ? "true" : "false") : "not_applicable")
           << "\nstep_0_weight_changed=" << (stepWeightDifference > 0.0f ? "true" : "false")
           << "\nstep_0_to_1_output_changed=" << (htp ? (stepOutputDifference > 0.0f ? "true" : "false") : "not_applicable")
           << "\nstep_0_weight_max_change=" << stepWeightDifference
           << "\nstep_0_to_1_output_max_change=" << stepOutputDifference
           << "\noutput_matches_updated_cpu_reference=" << (htp ? (everyOutputMatchedUpdatedReference ? "true" : "false") : "true") << '\n'
           << "npu_forward_used=" << (htp ? "true" : "false")
           << "\ncpu_fallback=false\ncpu_operations=loss,gradient,sgd_update"
           << "\nnpu_operations=" << (htp ? "forward" : "none")
           << "\nbackward_on_htp=false\noptimizer_on_htp=false\n"
           << "max_abs_error=" << (htp ? forwardError.maximum : 0.0f)
           << "\nnan_detected=" << (nonFinite ? "true" : "false")
           << "\ninf_detected=" << (nonFinite ? "true" : "false") << '\n'
           << "backend_create_time_us=" << (htp ? runtime.metrics().backendCreateUs : 0.0)
           << "\ndevice_create_time_us=" << (htp ? runtime.metrics().deviceCreateUs : 0.0)
           << "\ncontext_create_time_us=" << (htp ? runtime.metrics().contextCreateUs : 0.0)
           << "\ngraph_create_time_us=" << (htp ? runtime.metrics().graphCreateUs : 0.0)
           << "\ngraph_finalize_time_us=" << (htp ? runtime.metrics().graphFinalizeUs : 0.0)
           << "\nfirst_execute_time_us=" << (htp && !runtime.metrics().executeUs.empty() ? runtime.metrics().executeUs.front() : 0.0) << '\n';
    appendDistribution(stream, "steady_state_execute", executeStats);
    appendDistribution(stream, "runtime_weight_update", weightUpdateStats);
    appendDistribution(stream, "full_step", fullStepStats);
    appendDistribution(stream, "cpu_loss", distribution(lossTimes));
    appendDistribution(stream, "cpu_gradient", distribution(gradientTimes));
    appendDistribution(stream, "cpu_optimizer", distribution(optimizerTimes));
    stream << "total_training_time_us=" << elapsedUs(totalStarted) << '\n'
           << "loss_csv_begin\nstep,loss,cpu_reference_loss,weight_error,gradient_norm,update_norm,forward_us,loss_us,gradient_us,optimizer_us,runtime_weight_update_us,total_us\n";
    for (const auto& record : records) {
        stream << record.step << ',' << record.loss << ',' << record.cpuReferenceLoss << ','
               << record.weightError << ',' << record.gradientNorm << ',' << record.updateNorm << ','
               << record.forwardUs << ',' << record.lossUs << ',' << record.gradientUs << ','
               << record.optimizerUs << ',' << record.runtimeWeightUpdateUs << ',' << record.totalUs << '\n';
    }
    stream << "loss_csv_end\nerror=" << (success ? "none" : (error.empty() ? "training correctness condition failed" : error));
    return stream.str();
}
}

std::string runLinearExperiment(ExecutionMode mode, const TrainingConfig& config,
                                const LogSink& log) {
    std::string report;
    if (mode == ExecutionMode::QNN_CPU_MULTIBATCH_TRAINING ||
        mode == ExecutionMode::QNN_HTP_MULTIBATCH_TRAINING ||
        mode == ExecutionMode::QNN_CPU_TRAINING_BENCHMARK ||
        mode == ExecutionMode::QNN_HTP_TRAINING_BENCHMARK ||
        mode == ExecutionMode::QNN_HTP_DW_CHECK ||
        mode == ExecutionMode::QNN_HTP_FORWARD_HTP_DW_TRAINING ||
        mode == ExecutionMode::QNN_HTP_FORWARD_HTP_DW_BENCHMARK) {
        report = runTrainingBenchmarkExperiment(mode, config);
        if (log) log(report);
        return report;
    }
    if (mode == ExecutionMode::QNN_LINEAR_GRADIENT_CHECK) {
        report = gradientCheckReport(config);
    } else if (mode == ExecutionMode::QNN_CPU_LINEAR_TRAINING) {
        report = trainingReport(mode, config, false);
    } else if (mode == ExecutionMode::QNN_HTP_LINEAR_TRAINING) {
        report = trainingReport(mode, config, true);
    } else if (mode == ExecutionMode::QNN_HTP_DEVICE_PROBE) {
        Runtime probe;
        std::string error;
        const bool initialized = probe.initialize(QnnBackendKind::HTP, error);
        std::ostringstream stream;
        stream << "QNN_HTP_DEVICE_PROBE_RESULT\nexecution_mode=QNN_HTP_DEVICE_PROBE\nqnn_backend=HTP\n" << probe.diagnostics()
               << "backend_created=" << (probe.diagnostics().find("backend_create_result=0") != std::string::npos ? "true" : "false")
               << "\ndevice_created=" << (probe.diagnostics().find("device_create_result=0") != std::string::npos ? "true" : "false")
               << "\ncontext_created=" << (initialized ? "true" : "false")
               << "\ncpu_fallback=false\nstatus=" << (initialized ? "SUCCESS" : "FAILED")
               << "\nerror=" << (initialized ? "none" : error);
        report = stream.str();
    } else if (mode == ExecutionMode::QNN_HTP_FORWARD_DW_DX || mode == ExecutionMode::QNN_HTP_FULL_STEP) {
        report = failure(mode, "HTP", "mode_validation", "mode is outside forward+dW hybrid scope");
    } else {
        const bool htp = mode != ExecutionMode::QNN_CPU_FORWARD;
        const bool training = mode == ExecutionMode::QNN_HTP_FORWARD_CPU_BACKWARD || mode == ExecutionMode::QNN_HTP_FORWARD_DW;
        if (training) {
            report = trainingReport(mode, config, htp);
        } else {
            Runtime forward;
            std::string error;
            const char* backend = htp ? "HTP" : "CPU";
            if (!forward.initialize(htp ? QnnBackendKind::HTP : QnnBackendKind::CPU, error)) {
                report = failure(mode, backend, "runtime_initialize", error, forward.diagnostics());
            } else if (!forward.prepareMatMul(config.batchSize, config.dimension, config.dimension, false, error)) {
                report = failure(mode, backend, "forward_graph_prepare", error, forward.diagnostics());
            } else {
                const auto problem = makeProblem(config);
                std::vector<float> first, second;
                auto weight = problem.initialWeight.values;
                bool ok = forward.setInitialWeight(weight, error) && forward.executePrepared(problem.input.values, first, error);
                ErrorStats stats;
                if (ok) compare(first, cpu::matMul(problem.input, problem.initialWeight).values, stats);
                weight[0] += 0.03125f;
                ok = ok && forward.updateWeight(weight, error) && forward.executePrepared(problem.input.values, second, error);
                cpu::Matrix updated(config.dimension, config.dimension, weight);
                if (ok) compare(second, cpu::matMul(problem.input, updated).values, stats);
                const bool outputChanged = ok && maxDifference(first, second) > 0.0f;
                ok = ok && outputChanged && stats.maximum <= 1.0e-5f;
                std::ostringstream stream;
                stream << "QNN_EXPERIMENT_RESULT\nexecution_mode=" << executionModeName(mode)
                       << "\nqnn_backend=" << backend << "\nbackend_library=libQnn" << (htp ? "Htp.so" : "Cpu.so")
                       << "\nqnn_sdk_version=" << forward.info().sdkVersion
                       << "\nqnn_api_version=" << forward.info().apiVersion << '\n' << forward.diagnostics()
                       << "qnn_backend_initialized=true\nqnn_graph_finalized=true\ngraph_reused=true"
                       << "\ngraph_execute=" << (ok ? "success" : "failed")
                       << "\nruntime_weight_update=" << (ok ? "success" : "failed")
                       << "\nsecond_execute=" << (ok ? "success" : "failed")
                       << "\nruntime_weight_update_worked=" << (outputChanged ? "true" : "false")
                       << "\ntensor_dtype=FLOAT_32\ntensor_memory_type=RAW\nquantization=NONE"
                       << "\nforward_max_absolute_error=" << stats.maximum
                       << "\nforward_mean_absolute_error=" << (stats.count ? stats.sum / stats.count : 0.0)
                       << "\nforward_max_relative_error=" << stats.maxRelative
                       << "\ncpu_fallback=false\nnpu_forward_used=" << (htp ? "true" : "false")
                       << "\nnpu_forward_steps=2\nstatus=" << (ok ? "SUCCESS" : "FAILED")
                       << "\nerror=" << (ok ? "none" : error);
                report = stream.str();
            }
        }
    }
    if (log) log(report);
    return report;
}
}