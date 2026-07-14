# Vendor QNN version investigation: NX741J

## Outcome

Classification: **D, with one partial host metadata result**. The vendor host Build ID was recovered
from a normal bugreport, but the vendor API versions, version string, V81 stub Build ID, signed V81
skel Build ID, and skel interface version remain unavailable without vendor assistance or an exact
official firmware package.

## Firmware identity

| Item | Value |
|---|---|
| Model/device | nubia NX741J / PQ85A01 |
| Build | `MyOS16.0.28_NX741J_NEEA` |
| System/vendor incremental | `20260605.090729` / `20260605.093157` |
| Android/security patch | 16 (API 36) / 2026-05-01 |
| SoC / HTP | SM8850 / V81 |

## Vendor file access matrix

All six names are visible in directory listings. The directories are mode 0755, owned by
`root:shell`, and labelled `u:object_r:vendor_file:s0`. For each file, ordinary shell
`ls -l`, `ls -lZ`, `stat`, `sha256sum`, `toybox strings`, and `adb pull` returned Permission denied.
`readlink -f` returned the same absolute path but did not expose file contents.

| File | Shell read/pull | Normal absolute-path `dlopen` | Metadata obtained |
|---|---|---|---|
| `/vendor/lib64/libQnnHtp.so` | denied | `library ... not found` | Build ID `8b8abf9f1bb2483d` from bugreport |
| `/vendor/lib64/libQnnHtpPrepare.so` | denied | `library ... not found` | none |
| `/vendor/lib64/libQnnHtpV81.so` | denied | `library ... not found` | none |
| `/vendor/lib64/libQnnHtpV81Stub.so` | denied | `library ... not found` | none |
| `/vendor/lib64/libQnnSystem.so` | denied | `library ... not found` | none |
| `/vendor/lib/rfsa/adsp/libQnnHtpV81Skel.so` | denied | not attempted (DSP skel, not an Android host library) | none |

The metadata probe used only `dlopen`, `dl_iterate_phdr`, ELF `PT_NOTE`, and, if exported,
`QnnInterface_getProviders`. It never called backend/device/context/graph APIs. No namespace bypass
was attempted.

## Metadata by source

### Bugreport-derived firmware fact

An existing vendor camera-process stack listed `/vendor/lib64/libQnnHtp.so` with GNU Build ID
`8b8abf9f1bb2483d`. The bugreport did not expose its Core API, Backend API, version string, or the
V81 stub/skel metadata. Generic kernel FastRPC `-512` messages were not temporally tied to the QNN
probe and are not used as causal evidence.

### Runtime API result from an APK-derived, separate stack

The 143 OEM/system package candidates matching the requested terms were inspected using package
metadata and readable APK ZIP listings. Only `com.quicinc.voice.activation` contained QNN host
libraries. Its stack is explicitly V79 and is not assumed to match the firmware V81 stack.

| APK library | Size | GNU Build ID | Runtime API / notes |
|---|---:|---|---|
| `libQnnHtp.so` | 1,871,992 | `65947c2dac48b080` | Core 2.19.0; HTP Backend 5.26.0 |
| `libQnnSystem.so` | 264,160 | `f2f47b481a801a8b` | SONAME confirmed |
| `libQnnHtpV79Stub.so` | 433,656 | `5011f3bc62daaca0` | V79; depends on public `libcdsprpc.so` |

The APK and extracted binaries remain under ignored `build/` and are not redistributed.

### QAIRT 2.48.40 SDK comparison

| Source | Core API | HTP Backend API | Host Build ID | Stub Build ID | Architecture |
|---|---|---|---|---|---|
| QAIRT 2.48.40.260702151143 | 2.37.0 | 5.48.0 | `ac88579cae9476c2` | `afb3e455d95bdf97` | V81 |
| Firmware vendor host | unavailable | unavailable | `8b8abf9f1bb2483d` | unavailable | V81 by filenames/platform |
| VoiceActivation APK | 2.19.0 | 5.26.0 | `65947c2dac48b080` | `5011f3bc62daaca0` | V79 |

The vendor host Build ID does not match QAIRT 2.48.40. This proves the files are different builds;
it does not by itself prove API or host/stub/skel incompatibility.

## APEX, system, service, and linker findings

No QNN/HTP library was found in mounted APEX, `/system_ext`, `/product`, or `/odm`; product hits
were unrelated names containing `Stub` or `HEXAGON`. No QNN-specific Binder service or NNAPI device
service was listed. Vendor DSP and HexLP AIDL services exist, but ordinary `dumpsys` returned
`FAILED_TRANSACTION` and exposed no version. VINTF contains generic DSP/HexLP declarations, not a
public QNN runtime contract.

Only FastRPC libraries (`libadsprpc.so`, `libcdsprpc.so`, `libsdsprpc.so`) are public vendor native
libraries. QNN host/System libraries are absent from public library lists and ordinary shell
`dlopen` cannot load them.

## Maximum official log

The standalone NULL-config reproduction already uses the public maximum
`QNN_LOG_LEVEL_VERBOSE`. One additional run with that level confirmed:

- SM8850 platform detection;
- default platform info selected because no config was passed;
- default unsigned-PD setting;
- SDK `libQnnHtpV81Stub.so` loaded by absolute path;
- effective CDSP domain/session calculation;
- `qnn_open=0x80000600`, `getHandle=0x0f`, transport/skel 1002, then deviceCreate 14001.

It did not report vendor skel/stub version or interface identifiers.

## Version-family evaluation

Qualcomm's bundled official API revision histories state that Core API 2.19.0 and HTP API 5.26.0
were introduced in QNN SDK 2.26.0. Therefore the separate V79 APK stack is a **probable 2.26.x API
family**; its exact patch release is not established. This is not a candidate selected for the V81
firmware stack.

For the firmware vendor host, the compatible QAIRT/QNN release remains **insufficient information**.
No official Build-ID-to-release table was found, and no alternative SDK was installed or executed.

## Remaining vendor-only information

Qualcomm or nubia must provide the vendor host Core/Backend API, V81 stub/skel Build IDs and
interface version, the supported third-party access contract, and the QAIRT release compatible with
this signed firmware stack.
