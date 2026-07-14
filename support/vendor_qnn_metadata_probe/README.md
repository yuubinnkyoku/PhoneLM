# Vendor QNN metadata probe

This Android AArch64 command-line probe tests only whether a library can be
opened through normal `dlopen`. On success it reports the loaded ELF path, GNU
Build ID, SONAME when available, and QNN provider API versions when the library
exports `QnnInterface_getProviders`.

It does not use private linker namespaces, `android_dlopen_ext`, `LD_PRELOAD`,
injection, or elevated privileges. It never calls `backendCreate`,
`deviceCreate`, `contextCreate`, or any graph/DSP operation.

```powershell
.\support\vendor_qnn_metadata_probe\run.ps1 `
  -SdkRoot "<QAIRT-SDK-root>" `
  -DeviceSerial "<current-adb-serial>" `
  -LibraryPath "/vendor/lib64/libQnnHtp.so"
```

The build uses QAIRT headers only. No QAIRT or vendor binary is copied into the
repository. Raw results are appended under the ignored `diagnostics/local/`
directory and may contain the local ADB endpoint.
