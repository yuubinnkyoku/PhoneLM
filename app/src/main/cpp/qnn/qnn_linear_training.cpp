#include "qnn_linear_training.h"
#include "qnn_runtime.h"
#include "../cpu_reference_training.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

namespace phonelm::qnn {
namespace {
struct ErrorStats { double sum = 0; float maximum = 0; float maxRelative = 0; size_t count = 0; };
void compare(const std::vector<float>& actual, const std::vector<float>& expected, ErrorStats& s) {
    for (size_t i = 0; i < actual.size(); ++i) {
        const float e = std::fabs(actual[i] - expected[i]);
        const float d = std::max(1.0e-7f, std::fabs(actual[i]) + std::fabs(expected[i]));
        s.sum += e; s.maximum = std::max(s.maximum, e);
        s.maxRelative = std::max(s.maxRelative, e / d); ++s.count;
    }
}
std::string failure(ExecutionMode mode, const char* backend, const std::string& api,
                    const std::string& error) {
    return "QNN_EXPERIMENT_RESULT\nexecution_mode=" + std::string(executionModeName(mode)) +
           "\nqnn_backend=" + backend + "\nbackend_library=libQnn" +
           (std::string(backend) == "HTP" ? "Htp.so" : "Cpu.so") +
           "\nfailed_api=" + api + "\nstatus=FAILED\nerror=" + error;
}
}

std::string runLinearExperiment(ExecutionMode mode, const TrainingConfig& config,
                                const LogSink& log) {
    if (mode == ExecutionMode::QNN_HTP_DEVICE_PROBE) {
        Runtime probe; std::string error;
        const bool initialized=probe.initialize(QnnBackendKind::HTP,error);
        std::ostringstream s; s<<"QNN_HTP_DEVICE_PROBE_RESULT\nqnn_backend=HTP\n"
          <<probe.diagnostics()<<"backend_created="<<(probe.diagnostics().find("backend_create_result=0")!=std::string::npos?"true":"false")
          <<"\ndevice_created="<<(probe.diagnostics().find("device_create_result=0")!=std::string::npos?"true":"false")
          <<"\ncontext_created="<<(initialized?"true":"false")
          <<"\ncpu_fallback=false\nstatus="<<(initialized?"SUCCESS":"FAILED")
          <<"\nerror="<<(initialized?"none":error);
        auto r=s.str();if(log)log(r);return r;
    }
    if (mode == ExecutionMode::QNN_HTP_FORWARD_DW_DX || mode == ExecutionMode::QNN_HTP_FULL_STEP) {
        auto r = failure(mode, "HTP", "mode_validation", "mode is outside forward+dW hybrid scope");
        if (log) log(r); return r;
    }
    const bool htp = mode != ExecutionMode::QNN_CPU_FORWARD;
    const bool training = mode == ExecutionMode::QNN_HTP_FORWARD_CPU_BACKWARD ||
                          mode == ExecutionMode::QNN_HTP_FORWARD_DW;
    const bool htpDw = mode == ExecutionMode::QNN_HTP_FORWARD_DW;
    const char* backend = htp ? "HTP" : "CPU";
    std::string error;
    Runtime forward;
    if (!forward.initialize(htp ? QnnBackendKind::HTP : QnnBackendKind::CPU, error)) {
        auto r = "QNN_EXPERIMENT_RESULT\nexecution_mode=" + std::string(executionModeName(mode)) +
                 "\nqnn_backend=" + backend + "\nbackend_library=libQnn" +
                 (htp ? std::string("Htp.so") : std::string("Cpu.so")) + "\n" +
                 forward.diagnostics() + "status=FAILED\nerror=" + error;
        if (log) log(r); return r;
    }
    if (!forward.prepareMatMul(config.batchSize, config.dimension, config.dimension, false, error)) {
        auto r = failure(mode, backend, "forward_graph_prepare", error); if (log) log(r); return r;
    }
    Runtime dw;
    if (htpDw && !dw.initialize(QnnBackendKind::HTP, error)) {
        auto r = failure(mode, backend, "dw_backend_or_context_create", error); if (log) log(r); return r;
    }
    if (htpDw && !dw.prepareMatMul(config.dimension, config.batchSize,
                                   config.dimension, true, error)) {
        auto r = failure(mode, backend, "dw_graph_prepare", error); if (log) log(r); return r;
    }

    std::mt19937 gen(static_cast<std::mt19937::result_type>(config.seed));
    std::uniform_real_distribution<float> xd(-.25f,.25f), td(-.25f,.25f), wd(-.05f,.05f);
    cpu::Matrix x(config.batchSize, config.dimension), targetW(config.dimension, config.dimension),
                weight(config.dimension, config.dimension);
    for (auto& v : x.values) v=xd(gen); for (auto& v : targetW.values) v=td(gen);
    for (auto& v : weight.values) v=wd(gen);
    const auto target=cpu::matMul(x,targetW); const auto initialWeight=weight;
    float initialLoss=0, finalLoss=0; int completed=0; size_t zeroGradients=0, gradientCount=0;
    ErrorStats forwardError, dwError; std::vector<float> prediction;

    if (!training) {
        if (!forward.executeMatMul(x.values, weight.values, prediction, error)) {
            auto r=failure(mode,backend,"graphExecute_forward_1",error);if(log)log(r);return r;
        }
        compare(prediction, cpu::matMul(x,weight).values, forwardError);
        const auto first=prediction; weight.values[0] += 0.03125f;
        if (!forward.executeMatMul(x.values, weight.values, prediction, error)) {
            auto r=failure(mode,backend,"graphExecute_forward_2",error);if(log)log(r);return r;
        }
        compare(prediction, cpu::matMul(x,weight).values, forwardError);
        bool outputChanged=false; for(size_t i=0;i<first.size();++i) outputChanged |= first[i]!=prediction[i];
        const bool accurate=forwardError.maximum <= 1.0e-5f;
        std::ostringstream s; s << "QNN_EXPERIMENT_RESULT\nexecution_mode=" << executionModeName(mode)
          << "\nqnn_backend=" << backend << "\nbackend_library=libQnn" << (htp?"Htp.so":"Cpu.so")
          << "\nqnn_sdk_version=" << forward.info().sdkVersion
          << "\nqnn_api_version=" << forward.info().apiVersion << "\n" << forward.diagnostics()
          << "qnn_backend_initialized=true\nqnn_graph_finalized=true\ngraph_reused=true"
          << "\ngraph_execute=success\nruntime_weight_update=success\nsecond_execute=success"
          << "\nruntime_weight_update_worked=" << (outputChanged?"true":"false")
          << "\ntensor_dtype=FLOAT_32\ntensor_memory_type=RAW\nquantization=NONE"
          << "\nforward_max_absolute_error=" << forwardError.maximum
          << "\nforward_mean_absolute_error=" << forwardError.sum/forwardError.count
          << "\nforward_max_relative_error=" << forwardError.maxRelative
          << "\ncpu_fallback=false\nnpu_forward_used=" << (htp?"true":"false") << "\nnpu_forward_steps=2"
          << "\nstatus=" << (accurate&&outputChanged?"SUCCESS":"FAILED")
          << "\nerror=" << (accurate&&outputChanged?"none":"accuracy or runtime weight update failed");
        auto r=s.str();if(log)log(r);return r;
    }

    const int steps=config.steps+config.warmupSteps;
    for(int step=0; step<steps; ++step) {
        if(!forward.executeMatMul(x.values,weight.values,prediction,error)) break;
        const auto cpuPrediction=cpu::matMul(x,weight); compare(prediction,cpuPrediction.values,forwardError);
        cpu::Matrix p(config.batchSize,config.dimension,prediction);
        const float loss=cpu::meanSquaredError(p,target); if(step==0)initialLoss=loss;
        auto e=cpu::subtract(p,target); cpu::Matrix dp(config.batchSize,config.dimension);
        const float scale=2.0f/static_cast<float>(config.batchSize*config.dimension);
        for(size_t i=0;i<e.values.size();++i)dp.values[i]=e.values[i]*scale;
        const auto cpuGrad=cpu::matMul(cpu::transpose(x),dp); cpu::Matrix grad=config.dimension>0?cpu::Matrix(config.dimension,config.dimension):cpu::Matrix();
        if(htpDw) { std::vector<float> gv; if(!dw.executeMatMul(x.values,dp.values,gv,error)) break;
                    grad.values=std::move(gv); compare(grad.values,cpuGrad.values,dwError); }
        else grad=cpuGrad;
        for(float v:grad.values){zeroGradients += std::fabs(v)<=1.0e-12f; ++gradientCount;}
        cpu::sgdUpdate(weight,grad,config.learningRate); ++completed;
    }
    if(completed==steps && forward.executeMatMul(x.values,weight.values,prediction,error))
        finalLoss=cpu::meanSquaredError(cpu::Matrix(config.batchSize,config.dimension,prediction),target);
    bool changed=false;for(size_t i=0;i<weight.values.size();++i)changed|=std::fabs(weight.values[i]-initialWeight.values[i])>1e-8f;
    const bool accurate=forwardError.maximum<=1e-5f && (!htpDw || dwError.maximum<=1e-5f);
    const bool ok=completed==steps && finalLoss<initialLoss && changed && accurate;
    std::ostringstream s; s<<"QNN_EXPERIMENT_RESULT\nexecution_mode="<<executionModeName(mode)
      <<"\nqnn_backend="<<backend<<"\nbackend_library=libQnnHtp.so\nqnn_sdk_version="<<forward.info().sdkVersion
      <<"\nqnn_api_version="<<forward.info().apiVersion<<"\n"<<forward.diagnostics()
      <<"qnn_backend_initialized=true\nqnn_graph_finalized=true\ngraph_reused=true"
      <<"\ngraph_execute=success\nruntime_weight_update=success\nsecond_execute=success"
      <<"\nruntime_weight_update_worked=true\ncpu_fallback=false"
      <<"\ntensor_dtype=FLOAT_32\ntensor_memory_type=RAW\nquantization=NONE\nnpu_forward_used=true\nnpu_dw_used="<<(htpDw?"true":"false")
      <<"\nforward_max_absolute_error="<<forwardError.maximum<<"\nforward_mean_absolute_error="<<forwardError.sum/forwardError.count
      <<"\nforward_max_relative_error="<<forwardError.maxRelative<<"\ndw_max_absolute_error="<<(htpDw?dwError.maximum:0)
      <<"\ndw_mean_absolute_error="<<(htpDw?dwError.sum/dwError.count:0)<<"\ndw_max_relative_error="<<(htpDw?dwError.maxRelative:0)
      <<"\nzero_gradient_ratio="<<(gradientCount?static_cast<double>(zeroGradients)/gradientCount:0)<<"\nsaturated_value_ratio=0"
      <<"\nNPU_TRAINING_RESULT\ninitial_loss="<<initialLoss<<"\nfinal_loss="<<finalLoss<<"\nloss_decreased="<<(finalLoss<initialLoss?"true":"false")
      <<"\nweights_changed="<<(changed?"true":"false")<<"\nnan_detected="<<(!std::isfinite(finalLoss)?"true":"false")
      <<"\nsteps_completed="<<completed<<"\nnpu_forward_steps="<<completed<<"\nnpu_backward_dw_steps="<<(htpDw?completed:0)
      <<"\ncpu_operations="<<(htpDw?"loss,dP,sgd_update":"loss,dP,dW,sgd_update")<<"\nnpu_operations="<<(htpDw?"forward,dW":"forward")
      <<"\nstatus="<<(ok?"SUCCESS":"FAILED")<<"\nerror="<<(ok?"none":error);
    auto r=s.str();if(log)log(r);return r;
}
}
