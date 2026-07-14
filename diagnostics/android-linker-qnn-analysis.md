# Android linker and public-library analysis for QNN

Collected on 2026-07-14 from the NX741J production build using normal ADB shell access. Raw linker
configuration is retained only under ignored `diagnostics/local/`.

## Direct configuration facts

`/vendor/etc/public.libraries.txt` contains:

```text
libqti-perfd-client.so
libadsprpc.so
libcdsprpc.so
libsdsprpc.so
libfastcvopt.so
libOpenCL.so
```

It does not list `libQnnHtp.so`, `libQnnSystem.so`, `libQnnHtpPrepare.so`, or a QNN HTP stub.
`/system/etc/public.libraries.txt` exposes the Android NDK/platform set and
`libneuralnetworks.so`, but no QNN library. The corresponding system_ext and product files were
not present.

The generated `/linkerconfig/ld.config.txt` likewise has no QNN or HTP shared-library link entry.
The metadata probe executed from `/data/local/tmp` (the unrestricted shell test section) could not
open any tested absolute `/vendor/lib64/libQnn*.so` path: bionic returned `library ... not found`.
No private namespace, `android_dlopen_ext`, preload, injection, or SELinux workaround was used.

## Conclusions and limits

- **Direct fact:** FastRPC client libraries are public vendor native libraries.
- **Direct fact:** QNN host/System libraries are not declared public native libraries.
- **Direct fact:** an ordinary shell executable cannot directly load the tested vendor QNN paths.
- **Inference:** a normal third-party application should not expect the vendor QNN host libraries
  to be available through the public native-library namespace. This does not prove that every
  vendor/system process is denied; vendor processes have different linker namespaces.
- **Unconfirmed:** whether nubia supports third-party QNN HTP through an SDK-supplied host/stub and
  the signed vendor skel. That requires vendor documentation or support confirmation.
