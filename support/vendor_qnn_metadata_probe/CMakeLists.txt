cmake_minimum_required(VERSION 3.22.1)

project(vendor_qnn_metadata_probe LANGUAGES CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "This metadata probe must be cross-compiled for Android.")
endif()
if(NOT DEFINED QAIRT_SDK_ROOT OR NOT EXISTS "${QAIRT_SDK_ROOT}/include/QNN/QnnInterface.h")
    message(FATAL_ERROR "Set QAIRT_SDK_ROOT to a QAIRT installation containing QnnInterface.h.")
endif()

add_executable(vendor_qnn_metadata_probe main.cpp)
target_compile_features(vendor_qnn_metadata_probe PRIVATE cxx_std_17)
target_compile_options(vendor_qnn_metadata_probe PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(vendor_qnn_metadata_probe PRIVATE "${QAIRT_SDK_ROOT}/include/QNN")
target_link_libraries(vendor_qnn_metadata_probe PRIVATE dl)
