# QAIRT 2.48.40 official tool inventory

調査対象は `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702`。`sdk.yaml` は QAIRT `2.48.40`、build ID `260702151143`、QNN backend API package version `2.18.0`、Android NDK `r26c` を示す。Android実行ファイルの `--version` は `QNN SDK v2.48.40.260702151143`。

| 項目 | 実パス |
|---|---|
| qnn-net-run (Android AArch64) | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\bin\aarch64-android\qnn-net-run` |
| qnn-platform-validator (Android AArch64) | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\bin\aarch64-android\qnn-platform-validator` |
| qnn-model-lib-generator | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\bin\x86_64-windows-msvc\qnn-model-lib-generator` |
| qnn-onnx-converter | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\bin\x86_64-windows-msvc\qnn-onnx-converter` |
| QNN SampleApp source | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\examples\QNN\SampleApp\SampleApp` |
| QNN SampleApp (Android arm64) | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\examples\QNN\SampleApp\SampleApp\libs\arm64-v8a\qnn-sample-app` |
| libQnnCpu.so | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\lib\aarch64-android\libQnnCpu.so` |
| libQnnHtp.so | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\lib\aarch64-android\libQnnHtp.so` |
| libQnnSystem.so | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\lib\aarch64-android\libQnnSystem.so` |
| libQnnHtpV81Stub.so | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\lib\aarch64-android\libQnnHtpV81Stub.so` |
| libQnnHtpPrepare.so | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\lib\aarch64-android\libQnnHtpPrepare.so` |
| converter model examples | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\examples\QNN\converter\models` |
| converter Android build templates | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\share\QNN\converter\jni` |
| official Android NetRun script | `C:\Qualcomm\AIStack\QAIRT\2.48.40.260702\examples\QNN\NetRun\android\android-qnn-net-run.sh` |
| Android HTP setup example | SDK documentationの `tutorial3.html` |

HTP実行に関係する同梱ライブラリとして、上記に加えて `libQnnHtpNetRunExtensions.so`、`libQnnHtpProfilingReader.so`、`libQnnHtpOptraceProfilingReader.so` がある。今回、公式backend extension schemaの `devices[].dsp_arch="v81"`、`pd_session="signed"`、`device_id=0` も検証した。

HTP SDK ELFの比較値:

| ファイル | size | GNU build ID | 埋め込みbuild version |
|---|---:|---|---|
| libQnnHtp.so | 3,760,136 | `ac88579cae9476c2` | `2.48.40.260702151143` |
| libQnnHtpPrepare.so | 87,913,152 | `7298f8eb1ef4f53b` | 同SDK |
| libQnnHtpV81Stub.so | 777,848 | `afb3e455d95bdf97` | 同SDK |
| libQnnSystem.so | 4,052,904 | `ba7dd1d90db71683` | 同SDK |

各CLIの実測helpはGit対象外の `diagnostics/local/tool-help/` に保存した。Androidバイナリは端末上で実行した。

- `qnn-net-run-android-help.txt`, `qnn-net-run-android-version.txt`
- `qnn-platform-validator-android-help.txt`
- `qnn-sample-app-android-help.txt`
- `qnn-model-lib-generator-help.txt`
- `qnn-onnx-converter-help.txt`

引数はこれらのhelpとSDK同梱例から決定し、未確認のオプションは使用していない。
