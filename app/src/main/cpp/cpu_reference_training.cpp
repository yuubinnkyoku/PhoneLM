#include "cpu_reference_training.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace phonelm::cpu {
namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

float relativeError(float actual, float expected) {
    const float denominator = std::max(1.0e-6f, std::fabs(actual) + std::fabs(expected));
    return std::fabs(actual - expected) / denominator;
}

Matrix deterministicMatrix(int rows, int columns, float scale, float phase) {
    Matrix result(rows, columns);
    for (std::size_t index = 0; index < result.values.size(); ++index) {
        const float position = static_cast<float>(index + 1);
        result.values[index] = scale * std::sin(position * 0.73f + phase);
    }
    return result;
}

}  // namespace

Matrix::Matrix(int rowCount, int columnCount)
    : rows(rowCount),
      columns(columnCount),
      values(static_cast<std::size_t>(rowCount) * static_cast<std::size_t>(columnCount), 0.0f) {
    require(rowCount > 0 && columnCount > 0, "matrix dimensions must be positive");
}

Matrix::Matrix(int rowCount, int columnCount, std::vector<float> data)
    : rows(rowCount), columns(columnCount), values(std::move(data)) {
    require(rowCount > 0 && columnCount > 0, "matrix dimensions must be positive");
    require(values.size() == static_cast<std::size_t>(rows) * static_cast<std::size_t>(columns),
            "matrix data size does not match dimensions");
}

float& Matrix::at(int row, int column) {
    return values.at(static_cast<std::size_t>(row) * columns + column);
}

float Matrix::at(int row, int column) const {
    return values.at(static_cast<std::size_t>(row) * columns + column);
}

bool Matrix::sameShape(const Matrix& other) const {
    return rows == other.rows && columns == other.columns;
}

Matrix matMul(const Matrix& lhs, const Matrix& rhs) {
    require(lhs.columns == rhs.rows, "matMul inner dimensions do not match");
    Matrix output(lhs.rows, rhs.columns);
    for (int row = 0; row < lhs.rows; ++row) {
        for (int column = 0; column < rhs.columns; ++column) {
            double sum = 0.0;
            for (int inner = 0; inner < lhs.columns; ++inner) {
                sum += static_cast<double>(lhs.at(row, inner)) * rhs.at(inner, column);
            }
            output.at(row, column) = static_cast<float>(sum);
        }
    }
    return output;
}

Matrix transpose(const Matrix& input) {
    Matrix output(input.columns, input.rows);
    for (int row = 0; row < input.rows; ++row) {
        for (int column = 0; column < input.columns; ++column) {
            output.at(column, row) = input.at(row, column);
        }
    }
    return output;
}

Matrix subtract(const Matrix& lhs, const Matrix& rhs) {
    require(lhs.sameShape(rhs), "subtract shapes do not match");
    Matrix output(lhs.rows, lhs.columns);
    for (std::size_t index = 0; index < output.values.size(); ++index) {
        output.values[index] = lhs.values[index] - rhs.values[index];
    }
    return output;
}

float meanSquaredError(const Matrix& prediction, const Matrix& target) {
    const auto error = subtract(prediction, target);
    double sum = 0.0;
    for (const float value : error.values) {
        sum += static_cast<double>(value) * value;
    }
    return static_cast<float>(sum / static_cast<double>(error.values.size()));
}

ForwardBackwardResult forwardBackward(const Matrix& x,
                                      const Matrix& target,
                                      const Matrix& weight) {
    require(x.columns == weight.rows, "X and W shapes do not match");
    require(weight.rows == weight.columns, "W must be square");
    require(target.rows == x.rows && target.columns == weight.columns,
            "target shape must be [batch, dimension]");

    ForwardBackwardResult result;
    result.prediction = matMul(x, weight);
    result.error = subtract(result.prediction, target);
    result.loss = meanSquaredError(result.prediction, target);

    result.dPrediction = Matrix(result.error.rows, result.error.columns);
    const float scale = 2.0f /
                        static_cast<float>(result.error.rows * result.error.columns);
    for (std::size_t index = 0; index < result.error.values.size(); ++index) {
        result.dPrediction.values[index] = result.error.values[index] * scale;
    }

    result.dWeight = matMul(transpose(x), result.dPrediction);
    result.dInput = matMul(result.dPrediction, transpose(weight));
    return result;
}

void sgdUpdate(Matrix& weight, const Matrix& dWeight, float learningRate) {
    require(weight.sameShape(dWeight), "weight and gradient shapes do not match");
    require(std::isfinite(learningRate) && learningRate > 0.0f,
            "learning rate must be finite and positive");
    for (std::size_t index = 0; index < weight.values.size(); ++index) {
        weight.values[index] -= learningRate * dWeight.values[index];
    }
}

GradientCheckResult gradientCheck(int batchSize,
                                  int dimension,
                                  float epsilon,
                                  float absoluteTolerance,
                                  float relativeTolerance) {
    require(batchSize > 0 && dimension > 0, "gradient check dimensions must be positive");
    require(epsilon > 0.0f, "gradient check epsilon must be positive");

    Matrix x = deterministicMatrix(batchSize, dimension, 0.25f, 0.1f);
    const Matrix targetWeight = deterministicMatrix(dimension, dimension, 0.2f, 0.4f);
    const Matrix target = matMul(x, targetWeight);
    Matrix weight = deterministicMatrix(dimension, dimension, 0.08f, -0.3f);
    const auto analytical = forwardBackward(x, target, weight);

    GradientCheckResult result;
    for (std::size_t index = 0; index < weight.values.size(); ++index) {
        Matrix positive = weight;
        Matrix negative = weight;
        positive.values[index] += epsilon;
        negative.values[index] -= epsilon;
        const float numerical =
            (meanSquaredError(matMul(x, positive), target) -
             meanSquaredError(matMul(x, negative), target)) /
            (2.0f * epsilon);
        const float expected = analytical.dWeight.values[index];
        result.maxAbsoluteErrorWeight =
            std::max(result.maxAbsoluteErrorWeight, std::fabs(numerical - expected));
        result.maxRelativeErrorWeight =
            std::max(result.maxRelativeErrorWeight, relativeError(numerical, expected));
    }

    // Y is a fixed training target for this derivative, even though it was
    // generated once from the unperturbed X and W_target.
    for (std::size_t index = 0; index < x.values.size(); ++index) {
        Matrix positive = x;
        Matrix negative = x;
        positive.values[index] += epsilon;
        negative.values[index] -= epsilon;
        const float numerical =
            (meanSquaredError(matMul(positive, weight), target) -
             meanSquaredError(matMul(negative, weight), target)) /
            (2.0f * epsilon);
        const float expected = analytical.dInput.values[index];
        result.maxAbsoluteErrorInput =
            std::max(result.maxAbsoluteErrorInput, std::fabs(numerical - expected));
        result.maxRelativeErrorInput =
            std::max(result.maxRelativeErrorInput, relativeError(numerical, expected));
    }

    result.passed = result.maxAbsoluteErrorWeight <= absoluteTolerance &&
                    result.maxRelativeErrorWeight <= relativeTolerance &&
                    result.maxAbsoluteErrorInput <= absoluteTolerance &&
                    result.maxRelativeErrorInput <= relativeTolerance;
    return result;
}

CpuTrainingResult trainLinearRegression(int batchSize,
                                        int dimension,
                                        int steps,
                                        float learningRate,
                                        std::uint64_t seed) {
    require(batchSize > 0 && dimension > 0 && steps > 0, "training sizes must be positive");
    require(std::isfinite(learningRate) && learningRate > 0.0f,
            "learning rate must be finite and positive");

    std::mt19937 generator(static_cast<std::mt19937::result_type>(seed));
    std::uniform_real_distribution<float> inputDistribution(-0.25f, 0.25f);
    std::uniform_real_distribution<float> targetDistribution(-0.25f, 0.25f);
    std::uniform_real_distribution<float> initialDistribution(-0.05f, 0.05f);
    Matrix x(batchSize, dimension);
    Matrix targetWeight(dimension, dimension);
    Matrix weight(dimension, dimension);
    for (float& value : x.values) value = inputDistribution(generator);
    for (float& value : targetWeight.values) value = targetDistribution(generator);
    for (float& value : weight.values) value = initialDistribution(generator);
    const Matrix initialWeight = weight;
    const Matrix target = matMul(x, targetWeight);

    CpuTrainingResult result;
    result.lossHistory.reserve(static_cast<std::size_t>(steps) + 1);
    for (int step = 0; step < steps; ++step) {
        const auto state = forwardBackward(x, target, weight);
        if (step == 0) result.initialLoss = state.loss;
        result.lossHistory.push_back(state.loss);
        if (!std::isfinite(state.loss)) {
            result.nanDetected = true;
            break;
        }
        sgdUpdate(weight, state.dWeight, learningRate);
    }
    result.finalLoss = meanSquaredError(matMul(x, weight), target);
    result.lossHistory.push_back(result.finalLoss);
    result.lossDecreased = std::isfinite(result.finalLoss) &&
                           result.finalLoss < result.initialLoss * 0.95f;
    result.weightsChanged = false;
    for (std::size_t index = 0; index < weight.values.size(); ++index) {
        if (std::fabs(weight.values[index] - initialWeight.values[index]) > 1.0e-8f) {
            result.weightsChanged = true;
            break;
        }
    }
    result.nanDetected = result.nanDetected || !std::isfinite(result.finalLoss);
    return result;
}

}  // namespace phonelm::cpu

