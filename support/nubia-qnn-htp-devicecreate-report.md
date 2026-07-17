> **HISTORICAL_RESULT_SUPERSEDED (2026-07-17):** This document records the earlier vendor-only DSP
> search condition. Subsequent controlled tests succeeded with both QAIRT 2.47 and 2.48 when the
> matching V81 Stub and unsigned V81 Skel came from the same SDK and the Skel was available through
> an app-private, process-local DSP search path. The earlier `deviceCreate=14001` result is not a
> QAIRT 2.48 regression and does not mean that the device disallows HTP. See
> `docs/qnn-htp-qairt-2.48.md` for the current configuration. Qualcomm binary redistribution remains
> a separate, unresolved license question.
# QNN HTP initialization failure on nubia NX741J / Z80 Ultra

Hello nubia Support,

We are testing Qualcomm's public QNN API on an NX741J / Z80 Ultra running Android 16. CPU execution works correctly, but HTP initialization fails before any AI model or graph is created.

The failure is reproducible in three independent paths:

| Path | Process | Device config | Result |
|---|---|---|---|
| Android test application | third-party application | NULL | `QnnDevice_create=14001` |
| Qualcomm QAIRT 2.48.40 SampleApp sequence | Android shell | NULL | `QnnDevice_create=14001` |
| Standalone minimal program | Android shell | NULL | `QnnDevice_create=14001` |

Qualcomm's `qnn-net-run` also reports `Device Creation failure` and exits with code 11 when using the HTP backend. The same tool and test model run successfully with the QNN CPU backend. No CPU fallback occurs in the HTP test.

Environment: NX741J / PQ85A01, Android 16, SM8850, HTP V81, QAIRT 2.48.40.260702151143, QNN core API 2.37.0, HTP backend API 5.48.0.

The installed firmware is `MyOS16.0.28_NX741J_NEEA`. A normal diagnostic report shows that nubia's
vendor camera stack loads `/vendor/lib64/libQnnHtp.so` with Build ID `8b8abf9f1bb2483d`, while the
public QAIRT 2.48.40 library has a different Build ID. Android does not publish the vendor QNN host
library to ordinary third-party native-library namespaces. The vendor QNN API version and signed
V81 DSP-library interface version are not visible without firmware-team information.

Could you please help with these questions?

1. Is third-party use of the Hexagon HTP through Qualcomm QNN supported on the NX741J global firmware?
2. Which Qualcomm AI Runtime/QNN version matches the firmware's signed V81 DSP library?
3. Is an SDK, signed library, or vendor-specific configuration available for application developers?
4. Is the issue known on the current firmware?
5. Can this report be forwarded to the firmware or AI runtime engineering team?

The attached reproduction contains source code and anonymized results only. It contains no device serial, account information, Qualcomm SDK binaries, nubia firmware files, APK, or full device log.
