# QNN HTP deviceCreate minimal reproduction

外部提出用。ADB endpoint、ユーザー名、PCのローカル絶対パスは除去済み。

## Environment

- Device: nubia NX741J / Z80 Ultra (`PQ85A01`)
- SoC: Qualcomm SM8850
- Android: 16 / API 36
- Security patch: 2026-05-01
- Build fingerprint: `nubia/PQ85A01-UN/PQ85A01:16/BQ2A.250705.001-BP2A.250605.031.A3/20260605.090729:user/release-keys`
- Vendor fingerprint: `nubia/PQ85A01-UN/PQ85A01:16/BQ2A.250705.001-BP2A.250605.031.A3/20260605.093157:user/release-keys`
- QAIRT: 2.48.40, build `260702151143`
- SDK package backend API version: 2.18.0
- Runtime provider observed by PhoneLM: QNN core API 2.37.0, backend API 5.48.0
- HTP architecture: V81

## Minimal API reproduction

```cpp
Qnn_DeviceHandle_t deviceHandle = nullptr;
const Qnn_ErrorHandle_t result =
    qnnInterface.deviceCreate(logHandle, nullptr, &deviceHandle);
```

Observed result:

```text
device_create_result=14001
qnn_error=QNN_DEVICE_ERROR_INVALID_CONFIG
```

This is the same NULL-config call used by QAIRT 2.48.40 SampleApp.

## Independent official-tool reproduction

The SDK Android AArch64 `qnn-net-run` was used with a fixed FP32 model `P=MatMul(X,W)`, shapes `[2,4] x [4,4]`, both operands as inputs.

- Official qnn-net-run + QNN CPU: model load, graph compose/finalize/execute all succeeded; exit 0; output exactly matched the NumPy reference.
- Official qnn-net-run + QNN HTP: `Device Creation failure`; exit 11; context and graph were not reached.
- The documented HTP NetRun extension was also tested with `dsp_arch=v81`, `pd_session=signed`, and `device_id=0`, using the vendor signed skel search path. It still reported `Device Creation failure`; exit 11. The SDK unsigned skel was not installed on the production device.
- Official QAIRT SampleApp + the same model/backend/environment: `QnnDevice_create` with no config returned `0x36b1` (14001), then its NULL-device `QnnContext_create` path also returned `0x36b1`; exit 1.
- HTP CPU fallback did not occur.

Relevant backend trace:

```text
Backend build version: v2.48.40.260702151143
QnnBackend_create done successfully
Config not passed. Loading default platform info
Attempting to open libQnnHtpV81Stub.so using absolute filename
First connection to QNN stub established
DspTransport.openSession qnn_open failed, 0x80000600
DspTransport.getHandle failed, error 0x0000000f
Failed to create transport instance: 1002
Failed to load skel, error: 1002
exits device initialization with 14001
QnnDevice_create done. status 0x36b1
```

## Other observations

- PhoneLM CPU_REFERENCE and QNN CPU paths succeed.
- `libQnnHtp.so` loads and `backendCreate` succeeds before the failure.
- qnn-platform-validator reports HTP hardware Supported, FastRPC libraries Found, V81, but its unsigned calculator test fails with FastRPC `-6`.
- SDK HTP host build ID: `ac88579cae9476c2`; SDK V81 stub build ID: `afb3e455d95bdf97`.
- `/vendor/lib/rfsa/adsp/libQnnHtpV81Skel.so` exists, but shell cannot read/stat/pull it.
- Vendor QNN host/system/prepare/V81 stub filenames exist under `/vendor/lib64`, but shell cannot read/stat/pull them; their version/build IDs are therefore unknown.
- Readable vendor FastRPC libraries: `libadsprpc.so` build ID `74d243de0fa9d0a4b3c4b55c03147a9d`; `libcdsprpc.so` build ID `e7a4561f3a9c325ef43ff3ddd714cdb5`.
- Vendor public native library configuration exposes FastRPC libraries (`libadsprpc.so`, `libcdsprpc.so`, `libsdsprpc.so`) but does not expose vendor QNN host libraries.

## Scope of conclusion

Measured: the same 14001 is independently reproduced by the official QAIRT SampleApp in a shell process, and qnn-net-run fails at device creation. This excludes PhoneLM graph, tensor, MatMul, and device config as prerequisites for the failure.

Not yet established: the precise compatibility contract/version mismatch between this firmware's vendor HTP stack and QAIRT 2.48.40. Vendor QNN binaries were not readable and no official full firmware package was available for offline extraction.
