#include <QnnInterface.h>
#include <QnnSdkBuildId.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>

namespace {

using GetProvidersFn = Qnn_ErrorHandle_t (*)(const QnnInterface_t***, uint32_t*);

uint32_t errorCode(Qnn_ErrorHandle_t status) {
    return static_cast<uint32_t>(QNN_GET_ERROR_CODE(status));
}

void printResult(const char* name, Qnn_ErrorHandle_t status) {
    const uint32_t code = errorCode(status);
    std::printf("%s_decimal=%" PRIu32 "\n", name, code);
    std::printf("%s_hex=0x%04" PRIx32 "\n", name, code);
}

void logCallback(const char* format,
                 QnnLog_Level_t level,
                 uint64_t timestamp,
                 va_list arguments) {
    std::fprintf(stderr,
                 "qnn_log level=%d timestamp=%" PRIu64 " ",
                 static_cast<int>(level),
                 timestamp);
    std::vfprintf(stderr, format, arguments);
    const size_t length = std::strlen(format);
    if (length == 0 || format[length - 1] != '\n') {
        std::fputc('\n', stderr);
    }
}

}  // namespace

int main() {
    const char* sdkBuildId = QNN_SDK_BUILD_ID;
    if (sdkBuildId[0] == 'v') {
        ++sdkBuildId;
    }
    std::printf("qairt_version=%s\n", sdkBuildId);
    std::printf("backend_library=libQnnHtp.so\n");

    void* backendLibrary = dlopen("libQnnHtp.so", RTLD_NOW | RTLD_LOCAL);
    if (backendLibrary == nullptr) {
        std::printf("backend_library_load_result=failed\n");
        std::printf("backend_library_load_error=%s\n", dlerror());
        std::printf("context_create_called=false\n");
        return 2;
    }
    std::printf("backend_library_load_result=success\n");

    auto getProviders = reinterpret_cast<GetProvidersFn>(
        dlsym(backendLibrary, "QnnInterface_getProviders"));
    if (getProviders == nullptr) {
        std::printf("get_providers_result=symbol_missing\n");
        std::printf("context_create_called=false\n");
        dlclose(backendLibrary);
        return 3;
    }

    const QnnInterface_t** providers = nullptr;
    uint32_t providerCount = 0;
    const Qnn_ErrorHandle_t providerStatus = getProviders(&providers, &providerCount);
    printResult("get_providers_result", providerStatus);
    std::printf("provider_count=%" PRIu32 "\n", providerCount);
    if (providerStatus != QNN_SUCCESS || providers == nullptr) {
        std::printf("context_create_called=false\n");
        dlclose(backendLibrary);
        return 4;
    }

    QNN_INTERFACE_VER_TYPE qnn = QNN_INTERFACE_VER_TYPE_INIT;
    int32_t selectedProvider = -1;
    for (uint32_t index = 0; index < providerCount; ++index) {
        if (providers[index] == nullptr) {
            continue;
        }
        const Qnn_ApiVersion_t& version = providers[index]->apiVersion;
        const bool compatible =
            version.coreApiVersion.major == QNN_API_VERSION_MAJOR &&
            version.coreApiVersion.minor >= QNN_API_VERSION_MINOR;
        std::printf("provider_%" PRIu32 "_core_api_version=%" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
                    index,
                    version.coreApiVersion.major,
                    version.coreApiVersion.minor,
                    version.coreApiVersion.patch);
        std::printf("provider_%" PRIu32 "_backend_api_version=%" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
                    index,
                    version.backendApiVersion.major,
                    version.backendApiVersion.minor,
                    version.backendApiVersion.patch);
        std::printf("provider_%" PRIu32 "_compatible=%s\n", index, compatible ? "true" : "false");
        if (selectedProvider < 0 && compatible) {
            selectedProvider = static_cast<int32_t>(index);
            qnn = providers[index]->QNN_INTERFACE_VER_NAME;
        }
    }
    std::printf("selected_provider_index=%" PRId32 "\n", selectedProvider);
    if (selectedProvider < 0 || qnn.logCreate == nullptr || qnn.backendCreate == nullptr ||
        qnn.deviceCreate == nullptr || qnn.contextCreate == nullptr) {
        std::printf("provider_selection_result=failed\n");
        std::printf("context_create_called=false\n");
        dlclose(backendLibrary);
        return 5;
    }

    const Qnn_ApiVersion_t& selectedVersion = providers[selectedProvider]->apiVersion;
    std::printf("core_api_version=%" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
                selectedVersion.coreApiVersion.major,
                selectedVersion.coreApiVersion.minor,
                selectedVersion.coreApiVersion.patch);
    std::printf("backend_api_version=%" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
                selectedVersion.backendApiVersion.major,
                selectedVersion.backendApiVersion.minor,
                selectedVersion.backendApiVersion.patch);

    Qnn_LogHandle_t logHandle = nullptr;
    Qnn_BackendHandle_t backendHandle = nullptr;
    Qnn_DeviceHandle_t deviceHandle = nullptr;
    Qnn_ContextHandle_t contextHandle = nullptr;

    Qnn_ErrorHandle_t status = qnn.logCreate(logCallback, QNN_LOG_LEVEL_VERBOSE, &logHandle);
    printResult("log_create_result", status);
    if (status != QNN_SUCCESS) {
        std::printf("context_create_called=false\n");
        dlclose(backendLibrary);
        return 6;
    }

    status = qnn.backendCreate(logHandle, nullptr, &backendHandle);
    printResult("backend_create_result", status);
    std::printf("backend_handle_null=%s\n", backendHandle == nullptr ? "true" : "false");
    if (status != QNN_SUCCESS) {
        std::printf("context_create_called=false\n");
        if (qnn.logFree != nullptr) {
            qnn.logFree(logHandle);
        }
        dlclose(backendLibrary);
        return 7;
    }

    if (qnn.backendGetBuildId != nullptr) {
        const char* backendBuildId = nullptr;
        const Qnn_ErrorHandle_t buildIdStatus = qnn.backendGetBuildId(&backendBuildId);
        printResult("backend_build_id_result", buildIdStatus);
        if (buildIdStatus == QNN_SUCCESS && backendBuildId != nullptr) {
            std::printf("backend_build_id=%s\n", backendBuildId);
        }
    }

    std::printf("device_config=null\n");
    status = qnn.deviceCreate(logHandle, nullptr, &deviceHandle);
    printResult("device_create_result", status);
    std::printf("device_handle_null=%s\n", deviceHandle == nullptr ? "true" : "false");

    int exitCode = 0;
    if (status == QNN_SUCCESS) {
        std::printf("context_create_called=true\n");
        status = qnn.contextCreate(backendHandle, deviceHandle, nullptr, &contextHandle);
        printResult("context_create_result", status);
        std::printf("context_handle_null=%s\n", contextHandle == nullptr ? "true" : "false");
        if (status != QNN_SUCCESS) {
            exitCode = 9;
        }
    } else {
        std::printf("context_create_called=false\n");
        exitCode = 8;
    }

    if (contextHandle != nullptr && qnn.contextFree != nullptr) {
        printResult("context_free_result", qnn.contextFree(contextHandle, nullptr));
    }
    if (deviceHandle != nullptr && qnn.deviceFree != nullptr) {
        printResult("device_free_result", qnn.deviceFree(deviceHandle));
    }
    if (backendHandle != nullptr && qnn.backendFree != nullptr) {
        printResult("backend_free_result", qnn.backendFree(backendHandle));
    }
    if (logHandle != nullptr && qnn.logFree != nullptr) {
        printResult("log_free_result", qnn.logFree(logHandle));
    }
    dlclose(backendLibrary);
    return exitCode;
}
