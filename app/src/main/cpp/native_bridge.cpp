#include "benchmark_runner.h"

#include <android/log.h>
#include <jni.h>

#include <atomic>
#include <exception>
#include <string>

namespace {

constexpr const char* kLogTag = "PhoneLMBench";
std::atomic_bool gStopRequested{false};
std::atomic_bool gRunning{false};

void logcat(const std::string& message) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message.c_str());
}

jstring toJavaString(JNIEnv* env, const std::string& value) {
    return env->NewStringUTF(value.c_str());
}

std::string failedReport(const std::string& error) {
    return "RESULT\n"
           "backend_requested=UNKNOWN\n"
           "backend_actual=UNINITIALIZED\n"
           "fallback_detected=false\n"
           "nan_detected=false\n"
           "status=FAILED\n"
           "error=" + error;
}

class RunningGuard {
public:
    ~RunningGuard() {
        gRunning.store(false, std::memory_order_release);
    }
};

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_yuubinnkyoku_phonelm_NativeBridge_nativeGetEnvironmentInfo(
    JNIEnv* env, jobject /* receiver */) {
    try {
        const auto report = phonelm::BenchmarkRunner::environmentReport();
        logcat(report);
        return toJavaString(env, report);
    } catch (const std::exception& exception) {
        const auto report = failedReport(std::string("environment probe exception: ") + exception.what());
        logcat(report);
        return toJavaString(env, report);
    } catch (...) {
        const auto report = failedReport("environment probe unknown exception");
        logcat(report);
        return toJavaString(env, report);
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_yuubinnkyoku_phonelm_NativeBridge_nativeRunBenchmark(
    JNIEnv* env,
    jobject /* receiver */,
    jint backend,
    jint batchSize,
    jint dimension,
    jint steps,
    jint warmupSteps,
    jfloat learningRate,
    jlong seed,
    jobject progressCallback) {
    bool expected = false;
    if (!gRunning.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        const auto report = failedReport("a benchmark is already running");
        logcat(report);
        return toJavaString(env, report);
    }
    RunningGuard guard;
    gStopRequested.store(false, std::memory_order_release);

    jmethodID progressMethod = nullptr;
    if (progressCallback != nullptr) {
        jclass callbackClass = env->GetObjectClass(progressCallback);
        if (callbackClass != nullptr) {
            progressMethod = env->GetMethodID(
                callbackClass, "onNativeProgress", "(Ljava/lang/String;)V");
            env->DeleteLocalRef(callbackClass);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            progressMethod = nullptr;
        }
    }

    bool callbackEnabled = progressCallback != nullptr && progressMethod != nullptr;
    auto sink = [&](const std::string& message) {
        logcat(message);
        if (!callbackEnabled) {
            return;
        }
        jstring javaMessage = toJavaString(env, message);
        if (javaMessage == nullptr) {
            callbackEnabled = false;
            return;
        }
        env->CallVoidMethod(progressCallback, progressMethod, javaMessage);
        env->DeleteLocalRef(javaMessage);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            callbackEnabled = false;
            logcat("progress_callback_error=true");
        }
    };

    phonelm::TrainingConfig config;
    config.backend = static_cast<phonelm::BackendKind>(backend);
    config.batchSize = batchSize;
    config.dimension = dimension;
    config.steps = steps;
    config.warmupSteps = warmupSteps;
    config.learningRate = learningRate;
    config.seed = static_cast<std::uint64_t>(seed);

    try {
        const auto report = phonelm::BenchmarkRunner::run(config, gStopRequested, sink);
        return toJavaString(env, report);
    } catch (const std::exception& exception) {
        const auto report = failedReport(std::string("native exception: ") + exception.what());
        sink(report);
        return toJavaString(env, report);
    } catch (...) {
        const auto report = failedReport("unknown native exception");
        sink(report);
        return toJavaString(env, report);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_yuubinnkyoku_phonelm_NativeBridge_nativeRequestStop(
    JNIEnv* /* env */, jobject /* receiver */) {
    gStopRequested.store(true, std::memory_order_release);
    logcat("stop_requested=true");
}

JNIEXPORT jint JNI_OnLoad(JavaVM* /* vm */, void* /* reserved */) {
    logcat("jni_on_load=true");
    return JNI_VERSION_1_6;
}
