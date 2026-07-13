#include "qnn_runtime.h"

#include <QnnInterface.h>
#include <QnnOpDef.h>
#include <HTP/QnnHtpDevice.h>
#include <dlfcn.h>
#include <android/log.h>

#include <cstring>
#include <cstdlib>
#include <sstream>

namespace phonelm::qnn {
using GetProviders = Qnn_ErrorHandle_t (*)(const QnnInterface_t***, uint32_t*);

struct Runtime::Impl {
    void* transportLibrary = nullptr;
    void* library = nullptr;
    QNN_INTERFACE_VER_TYPE api = QNN_INTERFACE_VER_TYPE_INIT;
    Qnn_LogHandle_t log = nullptr;
    Qnn_BackendHandle_t backend = nullptr;
    Qnn_DeviceHandle_t device = nullptr;
    Qnn_ContextHandle_t context = nullptr;
    Qnn_GraphHandle_t graph = nullptr;
    Qnn_Tensor_t inputs[2] = {QNN_TENSOR_INIT, QNN_TENSOR_INIT};
    Qnn_Tensor_t output = QNN_TENSOR_INIT;
    uint32_t adims[2]{}, bdims[2]{}, odims[2]{};
    uint32_t outputElements = 0;
};

const char* backendKindName(QnnBackendKind kind) {
    return kind == QnnBackendKind::CPU ? "CPU" : "HTP";
}

Runtime::Runtime() : info_(queryBackendInfo()) {}
Runtime::~Runtime() {
    if (!impl_) return;
    if (impl_->context) impl_->api.contextFree(impl_->context, nullptr);
    if (impl_->device && impl_->api.deviceFree) impl_->api.deviceFree(impl_->device);
    if (impl_->backend) impl_->api.backendFree(impl_->backend);
    if (impl_->log && impl_->api.logFree) impl_->api.logFree(impl_->log);
    if (impl_->library) dlclose(impl_->library);
    if (impl_->transportLibrary) dlclose(impl_->transportLibrary);
    delete impl_;
}
const BackendInfo& Runtime::info() const { return info_; }
const std::string& Runtime::diagnostics() const { return diagnostics_; }

bool Runtime::initialize(QnnBackendKind kind, std::string& error) {
    impl_ = new Impl;
    std::ostringstream d;
    const char* library = kind == QnnBackendKind::CPU ? "libQnnCpu.so" : "libQnnHtp.so";
    d << "requested_backend=" << backendKindName(kind) << '\n'
      << "loaded_backend_library=" << library << '\n'
      << "header_qnn_api_version=" << QNN_API_VERSION_MAJOR << '.' << QNN_API_VERSION_MINOR << '.' << QNN_API_VERSION_PATCH << '\n';
    if (kind == QnnBackendKind::HTP) {
        setenv("ADSP_LIBRARY_PATH", "/vendor/lib/rfsa/adsp", 1);
        impl_->transportLibrary = dlopen("libQnnHtpV81Stub.so", RTLD_NOW | RTLD_GLOBAL);
        if (!impl_->transportLibrary) {
            error = std::string("library_load: dlopen(libQnnHtpV81Stub.so): ") + dlerror();
            diagnostics_=d.str()+"failed_api=library_load\n";
            return false;
        }
    }
    impl_->library = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    if (!impl_->library) { error = std::string("library_load: dlopen(") + library + "): " + dlerror(); diagnostics_=d.str()+"failed_api=library_load\n"; return false; }
    d << "library_load_result=0\n";
    auto getProviders = reinterpret_cast<GetProviders>(dlsym(impl_->library, "QnnInterface_getProviders"));
    if (!getProviders) { error = "get_providers: symbol missing"; diagnostics_=d.str()+"failed_api=get_providers\n"; return false; }
    const QnnInterface_t** providers = nullptr; uint32_t count = 0;
    if (getProviders(&providers, &count) != QNN_SUCCESS) { error = "get_providers: call failed"; diagnostics_=d.str()+"failed_api=get_providers\n"; return false; }
    d << "provider_count="<<count<<'\n'; int selected=-1;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& c=providers[i]->apiVersion.coreApiVersion; const auto& b=providers[i]->apiVersion.backendApiVersion;
        const bool ok=c.major==QNN_API_VERSION_MAJOR && c.minor>=QNN_API_VERSION_MINOR;
        d<<"provider_"<<i<<"_core_api_version="<<c.major<<'.'<<c.minor<<'.'<<c.patch<<'\n'
         <<"provider_"<<i<<"_backend_api_version="<<b.major<<'.'<<b.minor<<'.'<<b.patch<<'\n'
         <<"provider_"<<i<<"_compatible="<<(ok?"true":"false")<<'\n';
        if(selected<0&&ok){selected=static_cast<int>(i);impl_->api=providers[i]->QNN_INTERFACE_VER_NAME;}
    }
    d<<"selected_provider_index="<<selected<<'\n';
    if (!impl_->api.backendCreate) { error = "provider_select: compatible provider missing"; diagnostics_=d.str()+"failed_api=provider_select\n"; return false; }
    auto callback = [](const char* format, QnnLog_Level_t, uint64_t, va_list arguments) {
        __android_log_vprint(ANDROID_LOG_INFO, "PhoneLMQnn", format, arguments);
    };
    auto status = impl_->api.logCreate(callback, QNN_LOG_LEVEL_VERBOSE, &impl_->log);
    if (status != QNN_SUCCESS && status != QNN_COMMON_ERROR_NOT_SUPPORTED) {
        error = "log_create: logCreate=" + std::to_string(QNN_GET_ERROR_CODE(status)); diagnostics_=d.str()+"failed_api=log_create\n"; return false;
    }
    d<<"log_create_result="<<QNN_GET_ERROR_CODE(status)<<'\n';
    status = impl_->api.backendCreate(impl_->log, nullptr, &impl_->backend);
    d<<"backend_create_result="<<QNN_GET_ERROR_CODE(status)<<'\n';
    if (status != QNN_SUCCESS) { error = "backend_create: backendCreate=" + std::to_string(QNN_GET_ERROR_CODE(status)); diagnostics_=d.str()+"failed_api=backend_create\n"; return false; }
    if (impl_->api.deviceCreate) {
        d<<"device_create_called=true\ndevice_create_config_variant=OFFICIAL_SAMPLE_NULL\ndevice_config_pointer_null=true\nconfig_count=0\n";
        status = impl_->api.deviceCreate(impl_->log, nullptr, &impl_->device);
        d<<"device_create_result="<<QNN_GET_ERROR_CODE(status)<<"\ndevice_handle_null="<<(impl_->device?"false":"true")<<'\n';
        if (status != QNN_SUCCESS && status != QNN_DEVICE_ERROR_UNSUPPORTED_FEATURE) {
            error = "device_create: deviceCreate=" + std::to_string(QNN_GET_ERROR_CODE(status)); diagnostics_=d.str()+"context_create_called=false\nfailed_api=device_create\n"; return false;
        }
    }
    d<<"context_create_called=true\n";
    status = impl_->api.contextCreate(impl_->backend, impl_->device, nullptr, &impl_->context);
    d<<"context_create_result="<<QNN_GET_ERROR_CODE(status)<<"\ncontext_handle_null="<<(impl_->context?"false":"true")<<'\n';
    if (status != QNN_SUCCESS) { error = "context_create: contextCreate=" + std::to_string(QNN_GET_ERROR_CODE(status)); diagnostics_=d.str()+"failed_api=context_create\n"; return false; }
    diagnostics_=d.str()+"failed_api=none\n";
    return true;
}

static void tensor(Qnn_Tensor_t& t, const char* name, Qnn_TensorType_t type, uint32_t* dims) {
    t.version = QNN_TENSOR_VERSION_1;
    t.v1.name=name; t.v1.type=type; t.v1.dataFormat=QNN_TENSOR_DATA_FORMAT_DENSE;
    t.v1.dataType=QNN_DATATYPE_FLOAT_32; t.v1.rank=2; t.v1.dimensions=dims;
    t.v1.memType=QNN_TENSORMEMTYPE_RAW;
}

bool Runtime::prepareMatMul(uint32_t m, uint32_t k, uint32_t n, bool trans0, std::string& error) {
    if (!impl_ || !impl_->context) { error="runtime not initialized"; return false; }
    impl_->adims[0]=trans0?k:m; impl_->adims[1]=trans0?m:k;
    impl_->bdims[0]=k; impl_->bdims[1]=n; impl_->odims[0]=m; impl_->odims[1]=n;
    tensor(impl_->inputs[0], "a", QNN_TENSOR_TYPE_APP_WRITE, impl_->adims);
    tensor(impl_->inputs[1], "b", QNN_TENSOR_TYPE_APP_WRITE, impl_->bdims);
    tensor(impl_->output, "out", QNN_TENSOR_TYPE_APP_READ, impl_->odims);
    auto s=impl_->api.graphCreate(impl_->context, "phonelm_matmul", nullptr, &impl_->graph);
    if(s!=QNN_SUCCESS){error="graphCreate="+std::to_string(s);return false;}
    for(auto& t:impl_->inputs) if((s=impl_->api.tensorCreateGraphTensor(impl_->graph,&t))!=QNN_SUCCESS){error="tensorCreate(input)="+std::to_string(s);return false;}
    if((s=impl_->api.tensorCreateGraphTensor(impl_->graph,&impl_->output))!=QNN_SUCCESS){error="tensorCreate(output)="+std::to_string(s);return false;}
    Qnn_Scalar_t transpose = QNN_SCALAR_INIT; transpose.dataType=QNN_DATATYPE_BOOL_8; transpose.bool8Value=trans0;
    Qnn_Param_t param=QNN_PARAM_INIT; param.paramType=QNN_PARAMTYPE_SCALAR; param.name=QNN_OP_MAT_MUL_PARAM_TRANSPOSE_IN0; param.scalarParam=transpose;
    Qnn_OpConfig_t op=QNN_OPCONFIG_INIT; op.v1.name="matmul"; op.v1.packageName=QNN_OP_PACKAGE_NAME_QTI_AISW; op.v1.typeName=QNN_OP_MAT_MUL;
    op.v1.numOfParams=1; op.v1.params=&param; op.v1.numOfInputs=2; op.v1.inputTensors=impl_->inputs; op.v1.numOfOutputs=1; op.v1.outputTensors=&impl_->output;
    if((s=impl_->api.graphAddNode(impl_->graph,op))!=QNN_SUCCESS){error="graphAddNode="+std::to_string(s);return false;}
    if((s=impl_->api.graphFinalize(impl_->graph,nullptr,nullptr))!=QNN_SUCCESS){error="graphFinalize="+std::to_string(s);return false;}
    impl_->outputElements=m*n; return true;
}

bool Runtime::executeMatMul(const std::vector<float>& a,const std::vector<float>& b,std::vector<float>& out,std::string& error){
    out.resize(impl_->outputElements); impl_->inputs[0].v1.clientBuf={const_cast<float*>(a.data()),static_cast<uint32_t>(a.size()*4)};
    impl_->inputs[1].v1.clientBuf={const_cast<float*>(b.data()),static_cast<uint32_t>(b.size()*4)}; impl_->output.v1.clientBuf={out.data(),static_cast<uint32_t>(out.size()*4)};
    auto s=impl_->api.graphExecute(impl_->graph,impl_->inputs,2,&impl_->output,1,nullptr,nullptr);
    if(s!=QNN_SUCCESS){error="graphExecute="+std::to_string(s);return false;} return true;
}
}
