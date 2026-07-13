cmake_minimum_required(VERSION 3.22.1)

project(qnn_htp_device_create_repro LANGUAGES CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "This reproduction must be cross-compiled for Android.")
endif()
if(NOT DEFINED QAIRT_SDK_ROOT OR NOT EXISTS "${QAIRT_SDK_ROOT}/include/QNN/QnnInterface.h")
    message(FATAL_ERROR "Set QAIRT_SDK_ROOT to a QAIRT installation containing include/QNN/QnnInterface.h.")
endif()

add_executable(qnn_htp_device_create_repro main.cpp)
target_compile_features(qnn_htp_device_create_repro PRIVATE cxx_std_17)
target_compile_options(qnn_htp_device_create_repro PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(qnn_htp_device_create_repro PRIVATE "${QAIRT_SDK_ROOT}/include/QNN")
target_link_libraries(qnn_htp_device_create_repro PRIVATE dl log)
