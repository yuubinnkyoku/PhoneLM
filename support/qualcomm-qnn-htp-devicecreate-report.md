# QnnDevice_create returns QNN_DEVICE_ERROR_INVALID_CONFIG on SM8850 HTP V81 with QAIRT 2.48.40

## Summary

QNN CPU executes correctly, but QNN HTP device creation fails before context or graph creation. The same failure reproduces with an application, the official SampleApp initialization sequence, and a standalone program. The official `qnn-net-run` also stops at device creation.

The standalone reproduction contains no PhoneLM code, MNN, model, tensor, graph, context binary, device extension, PD selection, SoC ID, or core ID. It calls the generic SampleApp sequence with a NULL device config.

## Environment

| Item | Value |
|---|---|
| Device | nubia NX741J / Z80 Ultra |
| Device code | PQ85A01 |
| Android | 16 / API 36 |
| Security patch | 2026-05-01 |
| SoC | SM8850 |
| HTP architecture | V81 |
| QAIRT | 2.48.40.260702151143 |
| QNN core API | 2.37.0 |
| HTP backend API | 5.48.0 |
| Process domains tested | `shell`, `untrusted_app` |

ADB serials, network addresses, local usernames, local installation paths, and device-unique identifiers have been removed.

## Minimal call

```cpp
Qnn_DeviceHandle_t deviceHandle = nullptr;
Qnn_ErrorHandle_t status =
    qnnInterface.deviceCreate(logHandle, nullptr, &deviceHandle);
```

The standalone program dynamically loads `libQnnHtp.so`, resolves `QnnInterface_getProviders`, selects a compatible provider, creates the logger and backend, and then makes the call above. `contextCreate` is called only if `deviceCreate` succeeds.

## Directly observed results

| Path | Process | Device config | Direct result |
|---|---|---|---|
| PhoneLM device-only probe | `untrusted_app` | NULL | `QnnDevice_create=14001` (`0x36b1`) |
| QAIRT 2.48.40 official SampleApp sequence | `shell` | NULL | `QnnDevice_create=14001` (`0x36b1`) |
| Standalone reproduction supplied here | `shell` | NULL | `QnnDevice_create=14001` (`0x36b1`) |
| QAIRT `qnn-net-run`, QNN CPU | `shell` | N/A | Graph executed; exit 0; output exactly matched the CPU reference |
| QAIRT `qnn-net-run`, QNN HTP | `shell` | Tool-managed | `Device Creation failure`; exit 11 |

The standalone reproduction directly reported:

```text
backend_library_load_result=success
provider_count=1
core_api_version=2.37.0
backend_api_version=5.48.0
log_create_result_decimal=0
backend_create_result_decimal=0
device_config=null
device_create_result_decimal=14001
device_create_result_hex=0x36b1
device_handle_null=true
context_create_called=false
```

The verbose HTP backend trace immediately preceding 14001 contained:

```text
DspTransport.openSession qnn_open failed, 0x80000600
DspTransport.getHandle failed, error 0x0000000f
Failed to create transport instance: 1002
Failed to load skel, error: 1002
QnnDevice_create done. status 0x36b1
```

No CPU fallback occurred on the HTP paths. Context, graph, tensor, and operation APIs were not reached by the standalone reproduction or the PhoneLM device probe.

The confirmed one-shot run staged HTP Prepare following the official NetRun file set, but the backend explicitly logged that Prepare was not loaded. The attached public runner omits that unused library and deploys only the executable, HTP host library, and V81 stub.

`qnn-platform-validator` separately reported HTP hardware Supported, FastRPC libraries Found, and Hexagon V81. Its unsigned calculator test failed with FastRPC `-6`; testsig was not used.

## Interpretation boundary

Directly measured: three independent NULL-config call paths return the same 14001, including a standalone shell executable. This excludes PhoneLM graph/tensor/MatMul code and custom device-config construction as prerequisites for this failure.

Inference only: the evidence is consistent with an incompatibility or unsupported access path between the public QAIRT 2.48.40 host/backend/stub and the vendor-supplied signed HTP V81 stack. The precise compatibility contract and required vendor integration are not known because the vendor QNN host/stub/skel binaries are not readable by the shell. This report does not claim a confirmed version mismatch.

## Questions

1. Which QAIRT/QNN release is compatible with the signed HTP V81 stack shipped in this NX741J firmware?
2. Is QAIRT 2.48.40 host/backend/stub compatible with the vendor-provided signed `libQnnHtpV81Skel.so`?
3. Does SM8850 require a device extension configuration not used by the generic SampleApp?
4. Is direct QNN HTP access supported for third-party Android applications on this platform?
5. What do `qnn_open=0x80000600`, transport/skel error `1002`, and `QnnDevice_create=14001` indicate in this configuration?
6. Is a nubia-signed client component, context binary, or PD configuration required?
7. Can Qualcomm provide the expected host/stub/skel interface version for HTP V81 on SM8850?

## Reproduction attachments

The bundle contains the standalone source, build file, PowerShell runner, public result excerpt, anonymized device properties, validator summary, and file manifest. It intentionally excludes QAIRT binaries and headers, vendor files, APKs, models, raw tensors, and full Logcat output.
