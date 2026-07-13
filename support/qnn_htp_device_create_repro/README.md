# Standalone QNN HTP deviceCreate reproduction

This directory contains a PhoneLM-independent Android AArch64 reproduction for a QNN HTP initialization failure. It does not use the PhoneLM application, MNN, a model, tensors, or a graph.

The executable performs only these operations:

1. Loads `libQnnHtp.so` with `dlopen`.
2. Resolves and calls `QnnInterface_getProviders`.
3. Selects a provider whose QNN core API major matches the compile-time headers and whose minor version is at least the compile-time minor version.
4. Calls `logCreate`.
5. Calls `backendCreate` with a NULL config.
6. Calls `deviceCreate(logHandle, nullptr, &deviceHandle)`.
7. Prints the result in decimal and hexadecimal.
8. Calls `contextCreate` only if device creation succeeds.
9. Releases created resources in reverse order.

It deliberately contains no HTP custom configuration, signed/unsigned PD selection, SoC/core selection, tensor registration, graph creation, model loading, or context binary.

## Prerequisites

- Windows PowerShell
- Android SDK platform tools and CMake 3.22.1
- Android NDK r26c (the version declared by QAIRT 2.48.40)
- A local, licensed QAIRT 2.48.40 installation
- An online NX741J selected by its current `adb devices -l` serial

## Run once

```powershell
.\support\qnn_htp_device_create_repro\run.ps1 `
  -SdkRoot "<local-QAIRT-SDK-root>" `
  -DeviceSerial "<current-adb-serial>"
```

The script cross-compiles a statically linked C++ executable, deploys only the executable plus the local SDK HTP host and V81 stub under `/data/local/tmp/phonelm-qnn-device-create-repro`, records the shell/SELinux identity and deployed file list, and executes the probe exactly once. HTP Prepare is not needed before device creation and is not deployed by the public runner. The vendor skel is neither copied nor modified.

The raw local log is written to `diagnostics/local/minimal-device-create-repro.log`, which is intentionally excluded from Git and from the public support bundle.

No Qualcomm SDK binary, header, APK, model, raw tensor, vendor binary, ADB serial, or local SDK path is redistributed with this source reproduction.
