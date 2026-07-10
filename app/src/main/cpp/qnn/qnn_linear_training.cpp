#include "qnn_linear_training.h"

#include "qnn_backend_info.h"
#include "qnn_runtime.h"
#include "qnn_tensor.h"

#include <sstream>

namespace phonelm::qnn {

std::string runLinearExperiment(ExecutionMode mode,
                                const TrainingConfig& config,
                                const LogSink& log) {
    const auto info = queryBackendInfo();
    const bool htpRequested = mode != ExecutionMode::QNN_CPU_FORWARD;
    Runtime runtime;
    std::string initializationError;
    const bool initialized = runtime.initialize(htpRequested ? "HTP" : "CPU",
                                                initializationError);
    TensorDescriptor x{"X", {static_cast<std::size_t>(config.batchSize),
                              static_cast<std::size_t>(config.dimension)}};
    TensorDescriptor w{"W", {static_cast<std::size_t>(config.dimension),
                              static_cast<std::size_t>(config.dimension)}};

    std::ostringstream stream;
    stream << "QNN_EXPERIMENT_RESULT\n"
           << "execution_mode=" << executionModeName(mode) << '\n'
           << info.toLogString() << '\n'
           << "qnn_backend=" << (htpRequested ? "HTP" : "CPU") << '\n'
           << "qnn_backend_initialized=" << (initialized ? "true" : "false") << '\n'
           << "qnn_backend_library=NOT_VERIFIED\n"
           << "qnn_graph_name=NOT_CREATED\n"
           << "qnn_graph_finalized=false\n"
           << "input_x_shape=" << shapeString(x) << '\n'
           << "input_w_shape=" << shapeString(w) << '\n'
           << "tensor_dtype=UNVERIFIED\n"
           << "quantization_type=UNVERIFIED\n"
           << "quant_scale=UNVERIFIED\n"
           << "quant_offset=UNVERIFIED\n"
           << "host_to_npu_bytes=0\n"
           << "npu_to_host_bytes=0\n"
           << "quantize_time_ms=0\n"
           << "graph_execute_time_ms=0\n"
           << "dequantize_time_ms=0\n"
           << "cpu_reference_time_ms=0\n"
           << "max_absolute_error=UNAVAILABLE\n"
           << "mean_absolute_error=UNAVAILABLE\n"
           << "max_relative_error=UNAVAILABLE\n"
           << "npu_forward_used=false\n"
           << "npu_dw_used=false\n"
           << "npu_dx_used=false\n"
           << "npu_update_used=false\n"
           << "cpu_operations=none_executed\n"
           << "npu_operations=none\n"
           << "implementation_status=NOT_IMPLEMENTED\n"
           << "status=" << info.status << '\n'
           << "error=" << initializationError << '\n'
           << "NPU_TRAINING_RESULT\n"
           << "initial_loss=UNAVAILABLE\n"
           << "final_loss=UNAVAILABLE\n"
           << "loss_decreased=false\n"
           << "steps_completed=0\n"
           << "npu_forward_steps=0\n"
           << "npu_backward_dw_steps=0\n"
           << "npu_backward_dx_steps=0\n"
           << "npu_update_steps=0\n"
           << "cpu_operations=none_executed\n"
           << "npu_operations=none\n"
           << "status=" << info.status;
    const auto report = stream.str();
    if (log) log(report);
    return report;
}

}  // namespace phonelm::qnn
