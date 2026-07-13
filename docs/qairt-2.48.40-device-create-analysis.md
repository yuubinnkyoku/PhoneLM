# QAIRT 2.48.40 deviceCreate analysis

SDK: QAIRT `2.48.40.260702` (QNN API 2.37). The local installation path is intentionally omitted.

## Official SampleApp evidence

- `examples/QNN/SampleApp/SampleApp/src/Utils/DynamicLoadUtil.cpp:53-82` loads
  `QnnInterface_getProviders`, iterates every provider, and selects the first whose core major equals
  `QNN_API_VERSION_MAJOR` and whose core minor is at least `QNN_API_VERSION_MINOR`.
- `examples/QNN/SampleApp/SampleApp/src/QnnSampleApp.cpp:213` calls `backendCreate` with the QNN log
  handle and backend config.
- `examples/QNN/SampleApp/SampleApp/src/QnnSampleApp.cpp:783-793` calls `deviceCreate(logHandle,
  nullptr, &deviceHandle)`. It treats only `QNN_DEVICE_ERROR_UNSUPPORTED_FEATURE` as non-fatal.
- `examples/QNN/SampleApp/SampleApp/src/QnnSampleApp.cpp:315-318` passes the resulting device handle
  to `contextCreate(backendHandle, deviceHandle, contextConfig, &context)`.
- The generic SampleApp does not supply an HTP custom device config, SoC ID, core ID, signed-PD, or
  unsigned-PD option.
- `examples/Genie/Genie/src/qualla/engines/qnn-api/QnnApi.cpp:427-444` optionally obtains configs
  from a backend extension. It copies them into a `std::vector<const QnnDevice_Config_t*>` sized
  `configCount + 1`, leaving the last element null, then passes `data()` to `deviceCreate`.

## PhoneLM difference and conclusion

PhoneLM initially passed a null-terminated empty array and later tested explicit SoC/signed-PD
variants. Those variants are not present in the generic official SampleApp and were removed from the
official-sample probe. The official variant is exactly `deviceCreate(logHandle, nullptr,
&deviceHandle)`. Configuration storage lifetime is therefore not relevant for that variant.

PhoneLM also explicitly loads the SM8850/V81 transport stub so Android linker errors are reported
before device creation. The manifest exposes the device-provided `libcdsprpc.so` to the application
linker namespace. No graph, tensor, MatMul, or fallback belongs in the device probe.

## HTP architecture evidence

- `include/QNN/HTP/QnnHtpDeviceConfigShared.h:24-33` defines V81 and newer architectures.
- `include/QNN/QnnTypes.h:1889` defines `QNN_SOC_MODEL_SM8850 = 87`.
- `lib/python/qti/aisw/converters/common/backend_aware_configs/htp_v2.json:55722` maps SM8850 to
  `v81`.
- The official platform validator reported `Hexagon Architecture V81` on NX741J.
