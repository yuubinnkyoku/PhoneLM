#include "mnn_training_test.h"

#include <cassert>
#include <cmath>
#include <string>

// This target is optional and is intended for host-side validation of the
// configuration guard without loading Android or MNN runtimes.
int main() {
    phonelm::TrainingConfig valid;
    std::string error;
    assert(phonelm::validateTrainingConfig(valid, error));

    valid.batchSize = 0;
    assert(!phonelm::validateTrainingConfig(valid, error));

    valid.batchSize = 8;
    valid.dimension = -1;
    assert(!phonelm::validateTrainingConfig(valid, error));

    valid.dimension = 128;
    valid.learningRate = std::nanf("");
    assert(!phonelm::validateTrainingConfig(valid, error));
    return 0;
}
