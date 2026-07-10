#include "cpu_reference_training.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace {

void near(float actual, float expected, float tolerance = 1.0e-5f) {
    assert(std::fabs(actual - expected) <= tolerance);
}

void testMatMulAndTranspose() {
    using phonelm::cpu::Matrix;
    const Matrix x(2, 2, {1.0f, 2.0f, 3.0f, 4.0f});
    const Matrix w(2, 2, {0.5f, -1.0f, 2.0f, 0.25f});
    const auto product = phonelm::cpu::matMul(x, w);
    near(product.at(0, 0), 4.5f);
    near(product.at(0, 1), -0.5f);
    near(product.at(1, 0), 9.5f);
    near(product.at(1, 1), -2.0f);

    const auto transposed = phonelm::cpu::transpose(x);
    near(transposed.at(0, 1), 3.0f);
    near(transposed.at(1, 0), 2.0f);
}

void testLossGradientsAndSgd() {
    using phonelm::cpu::Matrix;
    const Matrix x(2, 2, {1.0f, 2.0f, 3.0f, 4.0f});
    Matrix w(2, 2, {0.5f, -1.0f, 2.0f, 0.25f});
    const Matrix target(2, 2, {4.0f, 0.0f, 10.0f, -1.0f});
    const auto state = phonelm::cpu::forwardBackward(x, target, w);

    // E = [[0.5, -0.5], [-0.5, -1.0]], mean(E^2) = 0.4375.
    near(state.loss, 0.4375f);
    // dP = 2E/(B*D) = E/2.
    near(state.dPrediction.at(0, 0), 0.25f);
    near(state.dPrediction.at(1, 1), -0.5f);
    // dW = X^T dP.
    near(state.dWeight.at(0, 0), -0.5f);
    near(state.dWeight.at(0, 1), -1.75f);
    near(state.dWeight.at(1, 0), -0.5f);
    near(state.dWeight.at(1, 1), -2.5f);
    // dX = dP W^T.
    near(state.dInput.at(0, 0), 0.375f);
    near(state.dInput.at(0, 1), 0.4375f);
    near(state.dInput.at(1, 0), 0.375f);
    near(state.dInput.at(1, 1), -0.625f);

    phonelm::cpu::sgdUpdate(w, state.dWeight, 0.1f);
    near(w.at(0, 0), 0.55f);
    near(w.at(0, 1), -0.825f);
    near(w.at(1, 0), 2.05f);
    near(w.at(1, 1), 0.5f);
}

void testGradientCheck() {
    const auto check = phonelm::cpu::gradientCheck(2, 4, 1.0e-3f);
    std::cout << "gradient_check_max_abs_dw=" << check.maxAbsoluteErrorWeight << '\n'
              << "gradient_check_max_rel_dw=" << check.maxRelativeErrorWeight << '\n'
              << "gradient_check_max_abs_dx=" << check.maxAbsoluteErrorInput << '\n'
              << "gradient_check_max_rel_dx=" << check.maxRelativeErrorInput << '\n';
    assert(check.passed);
}

void testLossDecrease() {
    const auto result = phonelm::cpu::trainLinearRegression(8, 128, 100, 0.1f, 20'260'710ULL);
    std::cout << "cpu_initial_loss=" << result.initialLoss << '\n'
              << "cpu_final_loss=" << result.finalLoss << '\n';
    assert(result.lossDecreased);
    assert(result.weightsChanged);
    assert(!result.nanDetected);
    assert(result.lossHistory.size() == 101);
}

}  // namespace

int main() {
    testMatMulAndTranspose();
    testLossGradientsAndSgd();
    testGradientCheck();
    testLossDecrease();
    std::cout << "cpu_reference_tests=PASS\n";
    return 0;
}
