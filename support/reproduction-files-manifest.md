# Public reproduction bundle manifest

## Included files

| File | Purpose |
|---|---|
| `support/qualcomm-qnn-htp-devicecreate-report.md` | Detailed engineering report, measured evidence, interpretation boundary, and questions for Qualcomm |
| `support/nubia-qnn-htp-devicecreate-report.md` | Short support report suitable for forwarding to nubia firmware/runtime engineering |
| `support/reproduction-files-manifest.md` | This attachment and exclusion manifest |
| `support/device-properties-anonymized.txt` | Non-unique device/platform properties required to identify the affected platform |
| `support/qnn-platform-validator-summary.txt` | Short validator result; not the full device log |
| `support/qnn_htp_device_create_repro/README.md` | Prerequisites, scope, and one-shot reproduction instructions |
| `support/qnn_htp_device_create_repro/CMakeLists.txt` | Android AArch64 build definition; references a locally installed SDK without redistributing it |
| `support/qnn_htp_device_create_repro/main.cpp` | PhoneLM-independent NULL-config QNN HTP device creation probe |
| `support/qnn_htp_device_create_repro/run.ps1` | Build/deploy/run script with SDK root and current ADB serial supplied as local arguments |
| `support/qnn_htp_device_create_repro/result-public.txt` | Anonymized, minimal excerpt from the confirmed one-shot execution |
| `support/vendor_qnn_metadata_probe/README.md` | Scope and instructions for the metadata-only normal-dlopen probe |
| `support/vendor_qnn_metadata_probe/CMakeLists.txt` | Android AArch64 metadata-probe build definition |
| `support/vendor_qnn_metadata_probe/main.cpp` | Build-ID/SONAME/provider metadata probe; no QNN initialization calls |
| `support/vendor_qnn_metadata_probe/run.ps1` | Parameterized build/deploy/run script |
| `support/device-build-properties-anonymized.txt` | Installed firmware identity with endpoint and unique values removed |
| `support/vendor-qnn-metadata-results.txt` | Anonymized vendor/APK/SDK metadata comparison |
| `support/bugreport-qnn-summary.txt` | Anonymized QNN-only findings; not the full bugreport |
| `diagnostics/android-linker-qnn-analysis.md` | Public native-library and linker namespace analysis |
| `diagnostics/vendor-qnn-version-investigation.md` | Evidence-source-separated vendor version investigation |
| `diagnostics/official-firmware-vendor-qnn-analysis.md` | Official-only firmware availability investigation |

## Deliberately excluded

- `libQnnHtp.so`, `libQnnHtpV81Stub.so`, `libQnnHtpPrepare.so`, `libQnnHtpV81Skel.so`, and every other QAIRT or vendor binary
- QAIRT headers and substantial copied SDK source
- APKs, DLLs, executables, models, context binaries, `.raw` and `.bin` data
- Full Logcat and raw diagnostic logs
- ADB serials, IP addresses, port numbers, local usernames, account information, local drive layout, and absolute SDK installation paths
- Vendor firmware and files pulled from the device

## Human review note

The reports and source necessarily mention public library filenames such as `libQnnHtp.so`; these are textual references, not bundled binary files. The packaging script rejects binary entries with `.so`, `.dll`, `.exe`, `.apk`, `.raw`, or `.bin` extensions and scans text for local-path, network-address, ADB-service-serial, username, and full-Logcat patterns. A human should still review the ZIP before submitting it to an external party.
