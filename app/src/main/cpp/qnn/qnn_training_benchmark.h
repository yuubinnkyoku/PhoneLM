#pragma once

#include "../training_engine.h"

#include <string>

namespace phonelm::qnn {

std::string runTrainingBenchmarkExperiment(ExecutionMode mode,
                                           const TrainingConfig& config);

}  // namespace phonelm::qnn
