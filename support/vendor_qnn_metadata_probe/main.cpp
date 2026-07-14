#include <QnnInterface.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <string>

namespace {

using GetProvidersFn = Qnn_ErrorHandle_t (*)(const QnnInterface_t*** providerList,
                                              uint32_t* numProviders);

struct SearchContext {
    std::string requestedPath;
    std::string requestedBaseName;
    bool matched = false;
    bool buildIdFound = false;
    std::string buildId;
    std::string loadedPath;
    std::string soname;
};

std::string baseName(const std::string& path) {
    const size_t separator = path.find_last_of('/');
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

size_t align4(size_t value) {
    return (value + 3U) & ~size_t{3U};
}

std::string toHex(const unsigned char* data, size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(size * 2U, '0');
    for (size_t index = 0; index < size; ++index) {
        result[index * 2U] = digits[data[index] >> 4U];
        result[index * 2U + 1U] = digits[data[index] & 0x0fU];
    }
    return result;
}

int inspectModule(dl_phdr_info* info, size_t, void* opaque) {
    auto& context = *static_cast<SearchContext*>(opaque);
    const std::string path = info->dlpi_name == nullptr ? "" : info->dlpi_name;
    if (path.empty() || (path != context.requestedPath && baseName(path) != context.requestedBaseName)) {
        return 0;
    }

    context.matched = true;
    context.loadedPath = path;
    const ElfW(Dyn)* dynamic = nullptr;
    size_t dynamicCount = 0;

    for (ElfW(Half) index = 0; index < info->dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = info->dlpi_phdr[index];
        const auto* segment = reinterpret_cast<const unsigned char*>(info->dlpi_addr + header.p_vaddr);
        if (header.p_type == PT_NOTE) {
            size_t offset = 0;
            while (offset + sizeof(ElfW(Nhdr)) <= header.p_memsz) {
                const auto* note = reinterpret_cast<const ElfW(Nhdr)*>(segment + offset);
                offset += sizeof(ElfW(Nhdr));
                if (offset + align4(note->n_namesz) + align4(note->n_descsz) > header.p_memsz) {
                    break;
                }
                const auto* name = segment + offset;
                offset += align4(note->n_namesz);
                const auto* description = segment + offset;
                offset += align4(note->n_descsz);
                if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz >= 3U &&
                    std::memcmp(name, "GNU", 3U) == 0) {
                    context.buildId = toHex(description, note->n_descsz);
                    context.buildIdFound = true;
                }
            }
        } else if (header.p_type == PT_DYNAMIC) {
            dynamic = reinterpret_cast<const ElfW(Dyn)*>(segment);
            dynamicCount = header.p_memsz / sizeof(ElfW(Dyn));
        }
    }

    if (dynamic != nullptr) {
        const char* stringTable = nullptr;
        ElfW(Xword) sonameOffset = 0;
        bool hasSoname = false;
        for (size_t index = 0; index < dynamicCount && dynamic[index].d_tag != DT_NULL; ++index) {
            if (dynamic[index].d_tag == DT_STRTAB) {
                uintptr_t stringTableAddress = dynamic[index].d_un.d_ptr;
                if (stringTableAddress < info->dlpi_addr) {
                    stringTableAddress += info->dlpi_addr;
                }
                stringTable = reinterpret_cast<const char*>(stringTableAddress);
            } else if (dynamic[index].d_tag == DT_SONAME) {
                sonameOffset = dynamic[index].d_un.d_val;
                hasSoname = true;
            }
        }
        if (stringTable != nullptr && hasSoname) {
            context.soname = stringTable + sonameOffset;
        }
    }
    return 1;
}

void printVersion(const char* prefix, const Qnn_Version_t& version) {
    std::printf("%s=%" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
                prefix, version.major, version.minor, version.patch);
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 2 || argv[1][0] != '/') {
        std::fprintf(stderr, "usage: vendor_qnn_metadata_probe /absolute/path/to/library.so\n");
        return 64;
    }

    SearchContext context;
    context.requestedPath = argv[1];
    context.requestedBaseName = baseName(context.requestedPath);
    std::printf("requested_path=%s\n", context.requestedPath.c_str());
    std::printf("probe_scope=metadata_only\n");
    std::printf("qnn_initialization_called=false\n");

    dlerror();
    void* library = dlopen(context.requestedPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        const char* error = dlerror();
        std::printf("dlopen_result=FAILED\n");
        std::printf("dlerror=%s\n", error == nullptr ? "unavailable" : error);
        std::printf("provider_query_called=false\n");
        std::printf("build_id_available=false\n");
        return 2;
    }
    std::printf("dlopen_result=SUCCESS\n");

    dl_iterate_phdr(inspectModule, &context);
    std::printf("loaded_path=%s\n", context.matched ? context.loadedPath.c_str() : "unavailable");
    std::printf("gnu_build_id=%s\n", context.buildIdFound ? context.buildId.c_str() : "unavailable");
    std::printf("build_id_available=%s\n", context.buildIdFound ? "true" : "false");
    std::printf("dt_soname=%s\n", context.soname.empty() ? "unavailable" : context.soname.c_str());

    dlerror();
    auto getProviders = reinterpret_cast<GetProvidersFn>(dlsym(library, "QnnInterface_getProviders"));
    const char* symbolError = dlerror();
    if (getProviders == nullptr || symbolError != nullptr) {
        std::printf("provider_symbol_exported=false\n");
        std::printf("provider_query_called=false\n");
    } else {
        std::printf("provider_symbol_exported=true\n");
        std::printf("provider_query_called=true\n");
        const QnnInterface_t** providers = nullptr;
        uint32_t providerCount = 0;
        const Qnn_ErrorHandle_t result = getProviders(&providers, &providerCount);
        std::printf("provider_query_result_decimal=%" PRIu32 "\n",
                    static_cast<uint32_t>(QNN_GET_ERROR_CODE(result)));
        std::printf("provider_count=%" PRIu32 "\n", providerCount);
        if (result == QNN_SUCCESS && providers != nullptr) {
            for (uint32_t index = 0; index < providerCount; ++index) {
                if (providers[index] == nullptr) {
                    continue;
                }
                const std::string core = "provider_" + std::to_string(index) + "_core_api_version";
                const std::string backend = "provider_" + std::to_string(index) + "_backend_api_version";
                printVersion(core.c_str(), providers[index]->apiVersion.coreApiVersion);
                printVersion(backend.c_str(), providers[index]->apiVersion.backendApiVersion);
            }
        }
    }

    dlclose(library);
    return 0;
}
