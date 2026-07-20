#pragma once
#include "../training_engine.h"
#include <atomic>
#include <string>
namespace phonelm::qnn {
std::string runMlpExperiment(ExecutionMode mode, const TrainingConfig &config,
                             std::atomic_bool &stopRequested,
                             const LogSink &log);
}