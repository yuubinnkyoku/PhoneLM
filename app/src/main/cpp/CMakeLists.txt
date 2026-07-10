cmake_minimum_required(VERSION 3.22.1)

project(PhoneLMNative LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

get_filename_component(PHONELM_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(MNN_ROOT "${PHONELM_ROOT}/third_party/MNN")

if(NOT EXISTS "${MNN_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR
        "Pinned MNN source is missing at ${MNN_ROOT}. "
        "Run scripts/fetch_mnn.ps1 from the repository root first.")
endif()

# MNN 3.5.0, commit c35f14f3ab5cb65094863b9a0e888370b027a670.
# A monolithic shared library deliberately avoids static-link dead stripping of
# gradient registrations and GPU RuntimeCreator initializers.
set(MNN_BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
set(MNN_SEP_BUILD OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_FOR_ANDROID_COMMAND ON CACHE BOOL "" FORCE)
set(MNN_BUILD_TRAIN ON CACHE BOOL "" FORCE)
set(MNN_OPENCL ON CACHE BOOL "" FORCE)
set(MNN_VULKAN ON CACHE BOOL "" FORCE)
set(MNN_VULKAN_IMAGE ON CACHE BOOL "" FORCE)
set(MNN_ARM82 ON CACHE BOOL "" FORCE)

# SME2 and KleidiAI are unrelated to this FP32 GPU-training probe. Keeping them
# off also avoids making the CPU baseline depend on optional new CPU features.
set(MNN_SME2 OFF CACHE BOOL "" FORCE)
set(MNN_KLEIDIAI OFF CACHE BOOL "" FORCE)

set(MNN_USE_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
set(MNN_USE_LOGCAT ON CACHE BOOL "" FORCE)
set(MNN_BUILD_MINI OFF CACHE BOOL "" FORCE)
set(MNN_SKIPBUILD_GEOMETRY OFF CACHE BOOL "" FORCE)
set(MNN_REDUCE_SIZE OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_CONVERTER OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_QUANTOOLS OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_OPENCV OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_LLM OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_DIFFUSION OFF CACHE BOOL "" FORCE)
set(MNN_JNI OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(MNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(MNN_SUPPORT_TRANSFORMER_FUSE OFF CACHE BOOL "" FORCE)
set(MNN_GPU_TIME_PROFILE OFF CACHE BOOL "" FORCE)
set(MNN_GPU_TRACE OFF CACHE BOOL "" FORCE)

# EXCLUDE_FROM_ALL prevents MNN's desktop-oriented train demo executables from
# being built for Android. The MNN target still pulls the official MNNTrain
# object sources into libMNN.so because MNN_BUILD_TRAIN is ON.
add_subdirectory("${MNN_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/mnn" EXCLUDE_FROM_ALL)

add_library(phonelm_native SHARED
    native_bridge.cpp
    benchmark_runner.cpp
    mnn_training_test.cpp
)

target_include_directories(phonelm_native PRIVATE
    "${MNN_ROOT}/include"
    "${MNN_ROOT}/source"
    "${MNN_ROOT}/express"
    "${MNN_ROOT}/schema/current"
    "${MNN_ROOT}/tools/train/source/grad"
    "${MNN_ROOT}/tools/train/source/optimizer"
)

target_compile_options(phonelm_native PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(phonelm_native PRIVATE
    MNN
    android
    log
)
