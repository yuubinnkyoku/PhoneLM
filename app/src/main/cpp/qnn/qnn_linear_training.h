#pragma once

#include "../mnn_training_test.h"
#include "../training_engine.h"

#include <string>

namespace phonelm::qnn {

std::string runLinearExperiment(ExecutionMode mode,
                                const TrainingConfig& config,
                                const LogSink& log);

}  // namespace phonelm::qnn

