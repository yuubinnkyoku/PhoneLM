#pragma once

#include "mnn_training_test.h"

#include <atomic>
#include <string>

namespace phonelm {

class BenchmarkRunner {
public:
    static std::string run(const TrainingConfig& config,
                           std::atomic_bool& stopRequested,
                           const LogSink& log);

    static std::string environmentReport();
};

}  // namespace phonelm
