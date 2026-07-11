#pragma once

#include "../cpu_reference_training.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace phonelm::qnn::host {

// Fixed-shape MatMul executor boundary shared by the SDK-free mock test and a
// future audited QAIRT adapter. No QNN type or operation identifier is assumed.
class MatMulExecutor {
public:
    virtual ~MatMulExecutor() = default;

    virtual bool execute(const cpu::Matrix& lhs,
                         const cpu::Matrix& rhs,
                         cpu::Matrix& output,
                         std::string& error) = 0;
    virtual const char* backendName() const = 0;
    virtual bool graphFinalized() const = 0;
    virtual std::size_t graphBuildCount() const = 0;
    virtual std::size_t executionCount() const = 0;
};

// Test-only behavior implemented with the CPU reference MatMul. Its backend
// name explicitly says MOCK so it can never be mistaken for QNN CPU or HTP.
class MockFixedShapeMatMulExecutor final : public MatMulExecutor {
public:
    bool execute(const cpu::Matrix& lhs,
                 const cpu::Matrix& rhs,
                 cpu::Matrix& output,
                 std::string& error) override;
    const char* backendName() const override;
    bool graphFinalized() const override;
    std::size_t graphBuildCount() const override;
    std::size_t executionCount() const override;

private:
    bool prepared_ = false;
    int lhsRows_ = 0;
    int lhsColumns_ = 0;
    int rhsRows_ = 0;
    int rhsColumns_ = 0;
    std::size_t graphBuildCount_ = 0;
    std::size_t executionCount_ = 0;
};

struct HybridTrainingResult {
    float initialLoss = 0.0f;
    float finalLoss = 0.0f;
    bool lossDecreased = false;
    bool weightsChanged = false;
    bool nanDetected = false;
    int stepsCompleted = 0;
    int forwardSteps = 0;
    int dWeightSteps = 0;
    std::string forwardBackend;
    std::string dWeightBackend;
    std::string transposeLocation = "HOST_CPP";
    std::string error;
    std::vector<float> lossHistory;
};

HybridTrainingResult runHybridTraining(MatMulExecutor& forwardExecutor,
                                       MatMulExecutor& dWeightExecutor,
                                       int batchSize,
                                       int dimension,
                                       int steps,
                                       float learningRate,
                                       std::uint64_t seed);

}  // namespace phonelm::qnn::host
