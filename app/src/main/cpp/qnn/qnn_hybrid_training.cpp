#include "qnn_hybrid_training.h"

#include <cmath>
#include <random>
#include <stdexcept>

namespace phonelm::qnn::host {
namespace {

bool sameFixedShape(const cpu::Matrix& lhs,
                    const cpu::Matrix& rhs,
                    int lhsRows,
                    int lhsColumns,
                    int rhsRows,
                    int rhsColumns) {
    return lhs.rows == lhsRows && lhs.columns == lhsColumns && rhs.rows == rhsRows &&
           rhs.columns == rhsColumns;
}

bool anyWeightChanged(const cpu::Matrix& before, const cpu::Matrix& after) {
    if (!before.sameShape(after)) return true;
    for (std::size_t index = 0; index < before.values.size(); ++index) {
        if (std::fabs(before.values[index] - after.values[index]) > 1.0e-8f) return true;
    }
    return false;
}

}  // namespace

bool MockFixedShapeMatMulExecutor::execute(const cpu::Matrix& lhs,
                                           const cpu::Matrix& rhs,
                                           cpu::Matrix& output,
                                           std::string& error) {
    if (lhs.columns != rhs.rows) {
        error = "MOCK_MATMUL_SHAPE_MISMATCH";
        return false;
    }
    if (!prepared_) {
        lhsRows_ = lhs.rows;
        lhsColumns_ = lhs.columns;
        rhsRows_ = rhs.rows;
        rhsColumns_ = rhs.columns;
        prepared_ = true;
        ++graphBuildCount_;
    } else if (!sameFixedShape(lhs,
                               rhs,
                               lhsRows_,
                               lhsColumns_,
                               rhsRows_,
                               rhsColumns_)) {
        error = "MOCK_FIXED_SHAPE_GRAPH_REQUIRES_REBUILD";
        return false;
    }

    try {
        output = cpu::matMul(lhs, rhs);
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
    ++executionCount_;
    error.clear();
    return true;
}

const char* MockFixedShapeMatMulExecutor::backendName() const {
    return "MOCK_HOST_CPP";
}

bool MockFixedShapeMatMulExecutor::graphFinalized() const {
    return prepared_;
}

std::size_t MockFixedShapeMatMulExecutor::graphBuildCount() const {
    return graphBuildCount_;
}

std::size_t MockFixedShapeMatMulExecutor::executionCount() const {
    return executionCount_;
}

HybridTrainingResult runHybridTraining(MatMulExecutor& forwardExecutor,
                                       MatMulExecutor& dWeightExecutor,
                                       int batchSize,
                                       int dimension,
                                       int steps,
                                       float learningRate,
                                       std::uint64_t seed) {
    HybridTrainingResult result;
    result.forwardBackend = forwardExecutor.backendName();
    result.dWeightBackend = dWeightExecutor.backendName();
    if (batchSize <= 0 || dimension <= 0 || steps <= 0) {
        result.error = "training sizes must be positive";
        return result;
    }
    if (!std::isfinite(learningRate) || learningRate <= 0.0f) {
        result.error = "learning rate must be finite and positive";
        return result;
    }

    std::mt19937 generator(static_cast<std::mt19937::result_type>(seed));
    std::uniform_real_distribution<float> inputDistribution(-0.25f, 0.25f);
    std::uniform_real_distribution<float> targetDistribution(-0.25f, 0.25f);
    std::uniform_real_distribution<float> initialDistribution(-0.05f, 0.05f);
    cpu::Matrix x(batchSize, dimension);
    cpu::Matrix targetWeight(dimension, dimension);
    cpu::Matrix weight(dimension, dimension);
    for (float& value : x.values) value = inputDistribution(generator);
    for (float& value : targetWeight.values) value = targetDistribution(generator);
    for (float& value : weight.values) value = initialDistribution(generator);
    const cpu::Matrix initialWeight = weight;
    const cpu::Matrix target = cpu::matMul(x, targetWeight);
    const cpu::Matrix transposedX = cpu::transpose(x);
    result.lossHistory.reserve(static_cast<std::size_t>(steps) + 1);

    for (int step = 0; step < steps; ++step) {
        cpu::Matrix prediction;
        if (!forwardExecutor.execute(x, weight, prediction, result.error)) return result;
        ++result.forwardSteps;

        const cpu::Matrix error = cpu::subtract(prediction, target);
        const float loss = cpu::meanSquaredError(prediction, target);
        if (step == 0) result.initialLoss = loss;
        result.lossHistory.push_back(loss);
        if (!std::isfinite(loss)) {
            result.nanDetected = true;
            result.error = "non-finite loss";
            return result;
        }

        cpu::Matrix dPrediction(error.rows, error.columns);
        const float gradientScale = 2.0f / static_cast<float>(error.values.size());
        for (std::size_t index = 0; index < error.values.size(); ++index) {
            dPrediction.values[index] = error.values[index] * gradientScale;
        }

        cpu::Matrix dWeight;
        if (!dWeightExecutor.execute(transposedX, dPrediction, dWeight, result.error)) {
            return result;
        }
        ++result.dWeightSteps;
        cpu::sgdUpdate(weight, dWeight, learningRate);
        ++result.stepsCompleted;
    }

    // Host-side verification of the final master weight does not increment the
    // accelerator counters. A real QAIRT experiment must log this explicitly as
    // HOST_CPP validation, not as an additional NPU forward step.
    result.finalLoss = cpu::meanSquaredError(cpu::matMul(x, weight), target);
    result.lossHistory.push_back(result.finalLoss);
    result.lossDecreased = std::isfinite(result.finalLoss) &&
                           result.finalLoss < result.initialLoss;
    result.weightsChanged = anyWeightChanged(initialWeight, weight);
    result.nanDetected = result.nanDetected || !std::isfinite(result.finalLoss);
    return result;
}

}  // namespace phonelm::qnn::host
