#pragma once

#include <cstdint>
#include <vector>

namespace phonelm::cpu {

struct Matrix {
    int rows = 0;
    int columns = 0;
    std::vector<float> values;

    Matrix() = default;
    Matrix(int rows, int columns);
    Matrix(int rows, int columns, std::vector<float> values);

    float& at(int row, int column);
    float at(int row, int column) const;
    bool sameShape(const Matrix& other) const;
};

struct ForwardBackwardResult {
    Matrix prediction;
    Matrix error;
    Matrix dPrediction;
    Matrix dWeight;
    Matrix dInput;
    float loss = 0.0f;
};

struct GradientCheckResult {
    float maxAbsoluteErrorWeight = 0.0f;
    float maxRelativeErrorWeight = 0.0f;
    float maxAbsoluteErrorInput = 0.0f;
    float maxRelativeErrorInput = 0.0f;
    bool passed = false;
};

struct CpuTrainingResult {
    float initialLoss = 0.0f;
    float finalLoss = 0.0f;
    bool lossDecreased = false;
    bool weightsChanged = false;
    bool nanDetected = false;
    std::vector<float> lossHistory;
};

Matrix matMul(const Matrix& lhs, const Matrix& rhs);
Matrix transpose(const Matrix& input);
Matrix subtract(const Matrix& lhs, const Matrix& rhs);
float meanSquaredError(const Matrix& prediction, const Matrix& target);
ForwardBackwardResult forwardBackward(const Matrix& x, const Matrix& target, const Matrix& weight);
void sgdUpdate(Matrix& weight, const Matrix& dWeight, float learningRate);

GradientCheckResult gradientCheck(int batchSize = 2,
                                  int dimension = 4,
                                  float epsilon = 1.0e-3f,
                                  float absoluteTolerance = 2.0e-3f,
                                  float relativeTolerance = 2.0e-2f);

CpuTrainingResult trainLinearRegression(int batchSize,
                                        int dimension,
                                        int steps,
                                        float learningRate,
                                        std::uint64_t seed);

}  // namespace phonelm::cpu

