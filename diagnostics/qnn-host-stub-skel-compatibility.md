# QNN host/stub/skel compatibility evidence

Target: nubia NX741J, SM8850, Android 16. SDK: QAIRT 2.48.40.260702.

The APK-side files are SDK copies staged at build time and are not committed. Their ELF metadata and
hashes were collected with NDK `llvm-readelf` and PowerShell `Get-FileHash`. `libQnnHtpV81Stub.so` has a DT_NEEDED
entry for device-provided `libcdsprpc.so`.

Exact SDK metadata is recorded in `diagnostics/sdk-qnn-elf-metadata.txt`. Notable values:

- `libQnnHtp.so`: 3,760,136 bytes, build ID `ac88579cae9476c2`, SHA-256
  `6E6DA5284060CA4369BB6FD3C6F2B661A9CEBABB2CF4CF695CD77F558082265F`.
- `libQnnHtpPrepare.so`: 87,913,152 bytes, build ID `7298f8eb1ef4f53b`.
- `libQnnHtpV81Stub.so`: 777,848 bytes, build ID `afb3e455d95bdf97`, SHA-256
  `16188365D86EF09A9067E8E04B7FA1C604729D1870219B1BBF04EE904E6C3FFA`.
- All three host-side files are ELF64/AArch64. The V81 stub depends on `libcdsprpc.so`.

The device exposes `/vendor/lib/rfsa/adsp/libQnnHtpV81Skel.so`; shell directory listing confirms the
name but Android permissions prevent an unprivileged `adb pull`, size read, hashing, or `strings`.
The vendor file is not modified.

The QAIRT unsigned V81 calculator probe reached FastRPC but returned `-6` and the official validator
reported that testsig/unsigned images would be required. This is evidence that unsigned-PD execution
is not available through the tested non-root path. It is not evidence that signed-PD host/stub/skel
versions are compatible.

Current compatibility conclusion: HTP hardware and V81 transport prerequisites are present, but
host/stub/vendor-signed-skel compatibility is not proven. A `deviceCreate=14001` result must not be
described as a MatMul or graph failure.
