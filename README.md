# PhoneLM: CPU基準実装からQNN HTPへ学習演算を段階的に移すAndroid実験

## nubia Z80 Ultra実機結果（2026-07-12、QAIRT 2.48.40.260702）

対象はnubia NX741J、Android 16/API 36、SM8850（HTP V81）です。

| 項目 | 結果 |
|---|---|
| Android実機CPU_REFERENCE | 達成。loss 0.00120013591 → 0.0010884183 |
| QNN CPU forward | 達成。最大絶対誤差 9.31323e-10 |
| QNN CPU graph再利用 | 達成。runtime W変更後に出力更新 |
| QNN HTP backendCreate | 達成 |
| QNN HTP deviceCreate | 失敗。NULL configで14001 (`0x36b1`) |
| QNN HTP contextCreate | 未到達 |
| QNN HTP graphCreate | 未到達 |
| QNN HTP operation execution | なし |
| QNN HTP forward | 未達。deviceCreateで停止 |
| QNN HTP graph再利用 | 未実施（forward初期化で停止） |
| QNN HTP dW | 未実施（forward初期化で停止） |
| NPU生成dWによるloss低下 | 未達 |

HTP backendは`libQnnHtp.so`、transportはSDKの`libQnnHtpV81Stub.so`です。
Android linker namespaceには`libcdsprpc.so`をmanifestで公開しました。SDK公式
`qnn-platform-validator`はHTP V81 hardwareとFastRPCライブラリを検出しましたが、SDKの
unsigned calculator testはFastRPC `-6`（testsig/unsigned image拒否）でした。端末vendorには
`/vendor/lib/rfsa/adsp/libQnnHtpV81Skel.so`があります。signed-PD、default config、明示SoCの
各device configを検証後も、現在の失敗APIは`deviceCreate`、QNN error codeは14001
（`QNN_DEVICE_ERROR_INVALID_CONFIG`）です。CPU backendへのfallbackは行っていません。

device専用probeではproviderは1件で、core API 2.37.0、HTP backend API 5.48.0でした。
`library_load`、provider選択、`logCreate`、`backendCreate`は成功しています。QAIRT generic
SampleAppと同じ`deviceCreate(logHandle, nullptr, &deviceHandle)`で14001となり、
`contextCreate`、graph、HTP演算には到達していません。QAIRT公式`qnn-net-run`は同じ最小
MatMul modelをQNN CPUで正常実行しましたが、HTPでは`Device Creation failure`、exit 11でした。
さらにPhoneLM、MNN、model、tensor、graphを含まない独立最小再現もshell domainでNULL-config
`deviceCreate=14001`を返しました。したがってPhoneLM固有graphは直接原因から除外され、現在は
端末vendorのsigned HTP stackと公開QAIRT 2.48.40の互換性・サポート条件の回答待ちです。
deviceCreateが成立していないため、FP16化や量子化を試す段階ではありません。詳細は
`docs/qairt-2.48.40-device-create-analysis.md`と`support/qualcomm-qnn-htp-devicecreate-report.md`です。

実機テストは次で再現できます。

```powershell
.\scripts\run_qnn_device_tests.ps1 `
  -SdkRoot "$env:QAIRT_SDK_ROOT" `
  -DeviceSerial "<nubia serial>"
```

Androidアプリ内の数値的に正しいCPU線形回帰を基準に、QAIRTのQNN C/C++ APIを直接使って
`P = XW`、続いて`dW = transpose(X)dP`をQNN CPU、QNN HTPへ1演算ずつ移す実験です。
最初のNPU成功条件は、HTPが生成した`dW`をCPUのFP32 master weightへ適用し、複数stepで
lossが低下することです。速度は成功条件ではありません。

既存のMNN CPU・OpenCL・Vulkan経路は回帰対象として保持しますが、現在の主作業はQNNです。
Transformer、Attention、トークナイザ、LLM全体へはまだ進みません。

このREADMEは「ビルドできたこと」と「実機で成立したこと」を区別します。

## 現在の結論（2026-07-14）

CPU referenceとQNN CPUは成功しています。QNN HTPはbackendCreateまで成功しますが、公式
SampleApp相当のNULL configでdeviceCreateが14001を返します。同じ結果をPhoneLM
`untrusted_app`、公式SampleAppのshell実行、PhoneLM非依存の独立shell最小再現で直接確認しました。
公式`qnn-net-run`もHTP device creation段階で停止します。

現在のstatusは、端末vendorのsigned HTP V81 stackとQAIRT 2.48.40の互換性または第三者アプリ向け
サポート条件についてQualcomm/nubiaの回答待ちです。未確認のversion mismatchは断定しません。
HTP operation executionは0件であり、NPU trainingは未達です。

## 固定バージョン

| 項目 | バージョン |
|---|---|
| MNN | tag `3.5.0` |
| MNN commit | `c35f14f3ab5cb65094863b9a0e888370b027a670` |
| Android Gradle Plugin | 8.11.1 |
| Gradle | 8.13 |
| Kotlin | 2.1.0 |
| compileSdk / targetSdk | 35 |
| minSdk | 26 |
| NDK | 27.0.12077973 |
| CMake | 3.22.1 |
| ABI | `arm64-v8a`のみ |

ローカルのNDK 26.3.11579264は`build/cmake/android.toolchain.cmake`を欠く不完全な
インストールだったため、完全に導入されていたNDK 27.0.12077973へ固定しました。

## ソース取得

MNN本体はリポジトリへ複製せず、固定コミットを検証する取得スクリプトを使用します。

Windows PowerShell:

```powershell
.\scripts\fetch_mnn.ps1
```

macOS/Linux:

```sh
sh ./scripts/fetch_mnn.sh
```

スクリプトはtagを取得した後、`HEAD`が上記commitと完全一致しなければ失敗します。
`third_party/MNN/`はGit管理対象外です。

## ビルド

必要なもの:

- JDK 17
- Android SDK Platform 35
- Android NDK 27.0.12077973
- SDK CMake 3.22.1
- 上記スクリプトで取得したMNN

PowerShell例:

```powershell
$env:JAVA_HOME="<local-JDK-17-root>"
$env:ANDROID_HOME="<local-Android-SDK-root>"
.\gradlew.bat :app:assembleDebug
```

成果物:

```text
app/build/outputs/apk/debug/app-debug.apk
```

Release（ログは削除されません）:

```powershell
.\gradlew.bat :app:assembleRelease
```

`isMinifyEnabled=false`で、C++の`PhoneLMBench`ログもDebugマクロに依存しません。
ローカルで生成・確認した成果物:

| 成果物 | サイズ | SHA-256 |
|---|---:|---|
| `app-debug.apk`（debug署名済み） | 159,254,721 bytes | `2F3F236B9E0BB3E4D8B59FC6ABE1A2CBD6FBA902684CD2A1BA2D1CEE43FFA18E` |
| `app-release-unsigned.apk` | 3,418,790 bytes | `145589C6C4FEAF903BDD1EC734EFBA59AC23D060ADDB61CF6303DFC33B465147` |
| `app-debug-androidTest.apk` | 892,657 bytes | `BFFE9813C31A2C8F6657F3BBCBADE250006AEED841350368E206E9867E1BB615` |

Debug APKが大きいのはnative debug symbolを保持しているためです。Release APKは未署名なので、
配布・install前に通常のAndroid signingが必要です。

### APKへ同梱される共有ライブラリ

Debug APKで確認済み:

```text
lib/arm64-v8a/libMNN.so
lib/arm64-v8a/libphonelm_native.so
lib/arm64-v8a/libc++_shared.so
```

MNNを分割shared libraryまたはstatic libraryにせず、`MNN_SEP_BUILD=OFF`の
モノリシックshared libraryにした理由は、勾配登録オブジェクト、OpenCL creator、
Vulkan creatorがdead-stripまたは未ロードになる問題を避けるためです。

## MNN CMake設定

主要設定は[app/src/main/cpp/CMakeLists.txt](app/src/main/cpp/CMakeLists.txt)に固定しています。

| オプション | 値 | 理由 |
|---|---:|---|
| `MNN_BUILD_SHARED_LIBS` | ON | APKへ共有ライブラリとして同梱 |
| `MNN_SEP_BUILD` | OFF | Train/Express/GPU登録を1つの`libMNN.so`へ保持 |
| `MNN_BUILD_TRAIN` | ON | MNN-Trainの勾配とSGDを使用 |
| `MNN_OPENCL` | ON | Adreno OpenCL候補 |
| `MNN_VULKAN` | ON | Vulkan候補 |
| `MNN_VULKAN_IMAGE` | ON | MNN 3.5.0 image backendを使用 |
| `MNN_ARM82` | ON | ARMv8.2 FP16実装を含める |
| `MNN_USE_SYSTEM_LIB` | OFF | MNN wrapperによる端末runtimeの動的検出 |
| `MNN_KLEIDIAI` | OFF | GPU検証と無関係なCPU経路を除外 |
| `MNN_SME2` | OFF | CPU baselineを追加CPU機能へ依存させない |
| `MNN_BUILD_MINI` | OFF | Geometry/Trainに必要な演算を削除しない |
| `MNN_SKIPBUILD_GEOMETRY` | OFF | Expressのgeometry変換を保持 |

OpenCLは`MNN_GPU_MEMORY_BUFFER | MNN_GPU_TUNING_WIDE`、Vulkanは
`MNN_GPU_TUNING_WIDE`でExecutorを作ります。精度は`Precision_High`です。

## 構成

```text
app/src/main/java/com/yuubinnkyoku/phonelm/
  MainActivity.kt
  BenchmarkViewModel.kt
  BenchmarkConfig.kt
  BenchmarkResult.kt
  NativeBridge.kt

app/src/main/cpp/
  benchmark_runner.cpp/.h
  cpu_reference_training.cpp/.h
  mnn_training_test.cpp/.h
  native_bridge.cpp
  training_engine.cpp/.h
  qnn/
    qnn_backend_info.cpp/.h
    qnn_host_quantization.cpp/.h
    qnn_hybrid_training.cpp/.h
    qnn_linear_training.cpp/.h
    qnn_runtime_stub.cpp
    qnn_runtime.h
    qnn_tensor.cpp/.h

host_tests/
  cpu_reference_training_test.cpp
  qnn_sdk_independent_test.cpp

scripts/
  check_qairt.ps1 / check_qairt.sh
  fetch_mnn.ps1 / fetch_mnn.sh
  run_host_tests.ps1
```

実行方式は分離されています。

```text
CpuReferenceBackend (pure C++)
  forward -> loss -> dP -> dW/dX -> SGD

MnnTrainingBackend
  MNN CPU / OpenCL / Vulkan
  Express graph -> MNN-Train autodiff -> official SGD

QnnHtpExperimentalBackend
  SDK-independent interface, fixed-shape MatMul boundary and explicit blocked result
  (SDK未導入のためQNN API呼び出しなし)
```

## 数式

CPU基準実装は次を厳密に実装します。

```text
X:        [B, D]
W:        [D, D]
W_target: [D, D]
Y = X W_target
P = X W
E = P - Y

L  = sum(E^2) / (B D)
dP = 2 E / (B D)
dW = transpose(X) dP
dX = dP transpose(W)
W_next = W - learning_rate dW
```

MNN経路も`_MatMul`、`_Subtract`、`_Square`、`_ReduceMean`で同じlossを構築し、
`MNN::Train::SGD::step`へ渡します。

固定seedは`20260710`です。同じ端末・同じmode・同じ設定では、同じX、W_target、
初期Wを生成します。CPU referenceとMNNは同じ生成順を使い、XとW_targetは
`[-0.25, 0.25]`、初期Wは`[-0.05, 0.05]`です。

## UIと実行モード

従来のCPU、OpenCL、Vulkan、Stopボタンを維持しています。加えて実行mode Spinner、
CPU/QNN用preset、HTP forward、HTP forward+dWボタンがあります。

実行mode:

```text
CPU_REFERENCE
MNN_CPU
MNN_OPENCL
MNN_VULKAN
QNN_CPU_FORWARD
QNN_HTP_FORWARD
QNN_HTP_FORWARD_CPU_BACKWARD
QNN_HTP_FORWARD_DW
QNN_HTP_FORWARD_DW_DX
QNN_HTP_FULL_STEP
```

QNN CPU modeはQAIRT 2.48.40で実機実行済みです。HTP modeは全てdeviceCreateで停止し、
context、graph、tensor、演算へ進みません。

| mode | QNNへ移す処理 | CPUに残す処理 | 現在 |
|---|---|---|---|
| `QNN_CPU_FORWARD` | QNN CPU forward | 比較値 | SUCCESS、graph再利用確認済み |
| `QNN_HTP_FORWARD` | HTP forward | 比較値 | deviceCreate=14001で停止 |
| `QNN_HTP_FORWARD_CPU_BACKWARD` | HTP forward | loss、`dP`、`dW`、SGD | HTP初期化未成立のため未実施 |
| `QNN_HTP_FORWARD_DW` | HTP forward、`dW` | loss、`dP`、SGD | HTP初期化未成立のため未実施 |
| `QNN_HTP_FORWARD_DW_DX` | HTP forward、`dW`、`dX` | loss、`dP`、SGD | HTP初期化未成立のため未実施 |
| `QNN_HTP_FULL_STEP` | 成立した場合だけ全step | 未確定 | `NOT_IMPLEMENTED` |

Preset:

- Small: B=8、D=128、steps=100、warmup=0
- Benchmark: B=32、D=512、steps=200、warmup=20
- QNN first probe: B=2、D=4、steps=20、warmup=0

学習は単一のExecutorService上で動きます。UIスレッドはブロックしません。二重開始は
KotlinとJNIの両方で拒否します。Stopはatomic flagを設定し、現在のstep完了後に停止します。

## 実機実行

```powershell
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb logcat -c
adb logcat -s PhoneLMBench:I MNNJNI:I '*:S'
```

1. Small presetでCPUを実行します。
2. `status=SUCCESS`、`loss_decreased=true`、`weights_changed=true`を確認します。
3. 同じ設定のOpenCLとVulkanを別々に実行します。
4. `backend_actual`だけでなく、`executed_backends`と`fallback_operations`を確認します。
5. Benchmark presetでwarmup後のaverage/median/p95を比較します。

端末がthermal throttling中だと比較が歪むため、各modeの実行順を入れ替え、端末温度を
落ち着かせて複数回取得してください。

## ログ

開始時:

```text
backend_requested=OPENCL
backend_actual=OPENCL
mnn_version=3.5.0
mnn_commit=c35f14f3ab5cb65094863b9a0e888370b027a670
device_name=nubia ...
android_version=...
cpu_abi=arm64-v8a
optimizer=SGD
optimizer_host_sync=true
fallback_detection=per_op_output_backend_callback
```

step:

```text
backend_requested=OPENCL
backend_actual=OPENCL
batch_size=32
dimension=512
step=100
loss=0.012345
step_time_ms=18.420
fallback_detected=false
```

終了:

```text
RESULT
backend_requested=OPENCL
backend_actual=OPENCL
executed_backends=OPENCL
initial_loss=...
final_loss=...
average_step_time_ms=...
median_step_time_ms=...
p95_step_time_ms=...
total_time_ms=...
loss_decreased=true
weights_changed=true
nan_detected=false
fallback_detected=false
fallback_operations=none
optimizer_host_sync=true
status=SUCCESS
error=none
```

GPU runでCPU演算を検出した場合、計算が最後まで終わっても`status=FAILED`です。
`fallback_operations`には、phase、演算名、演算typeを最大32件記録します。

### `backend_actual`と`executed_backends`の違い

- `backend_actual`: 要求したMNN runtimeの初期化結果。
- `executed_backends`: callbackで演算出力tensorに紐づくbackendを観測した集合。
- `fallback_detected`: GPU要求時にCPU演算を観測、または要求GPUを一度も観測しなかった場合。

MNNのcallback公開APIは演算backendを直接返さないため、固定commitの内部
`TensorUtils::getDescribeOrigin(tensor)->getBackend()->type()`を使用しています。
これはこの固定版に対する診断実装です。

GPUの追加確認として、端末で許可される場合はPerfetto/Android GPU Inspector、または
Adreno KGSL busy counterを同時取得してください。`/sys/class/kgsl/kgsl-3d0/gpubusy`は
端末権限により読めないことがあります。busy counterだけをbackend証明には使わず、
アプリの演算backendログと組み合わせます。

## ホストテスト

```powershell
.\scripts\run_host_tests.ps1
```

確認済み出力:

```text
gradient_check_max_abs_dw=3.08501e-09
gradient_check_max_rel_dw=1.65244e-05
gradient_check_max_abs_dx=1.17143e-09
gradient_check_max_rel_dx=5.10085e-05
cpu_initial_loss=0.0663977
cpu_final_loss=0.0594779
cpu_reference_tests=PASS
mock_hybrid_initial_loss=0.00120014
mock_hybrid_final_loss=0.00108842
mock_hybrid_forward_steps=20
mock_hybrid_dw_steps=20
mock_vs_cpu_initial_loss_abs_error=0
mock_vs_cpu_final_loss_abs_error=0
qnn_sdk_independent_tests=PASS
```

このテストは次を個別にassertします。

- MatMul
- Transpose
- 全要素平均MSE
- `dW`
- `dX`
- SGD更新値
- B=2、D=4、epsilon=1e-3の数値微分
- B=8、D=128、100 stepのloss低下
- signed 8/16-bitへ流用できるSDK非依存のscale/zero point計算、量子化、逆量子化
- saturated value ratioとzero gradient ratio
- tensor要素数とbuffer byte数のoverflow検出
- 固定shape MatMul mockを1回だけprepareし、異なるruntime Wで複数回実行
- mock forward、mock `dW`、CPU loss/`dP`/SGDによるB=2、D=4、20 stepのloss低下

mockのbackend名は常に`MOCK_HOST_CPP`です。QNN CPU、QNN HTP、NPUとして集計しません。

Kotlin/JVMテスト:

```powershell
.\gradlew.bat :app:testDebugUnitTest
```

Android instrumentation（実機接続時）:

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest
```

instrumentation testにはCPU 100 step、同seed初期loss、重み更新、NaN、入力拒否、
step境界stopが含まれます。

## 技術調査結果: MNN 3.5.0

以下は固定commitの公式ソースを直接確認した結果です。

### Express / MNN-TrainとAndroid

- top-level CMakeに[`MNN_BUILD_TRAIN`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/CMakeLists.txt#L46)が存在し、
  Train sourcesを[`add_subdirectory(tools/train)`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/CMakeLists.txt#L787-L790)で組み込みます。
- 公式[`linearRegression.cpp`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/tools/train/source/demo/linearRegression.cpp#L16-L51)は
  `_TrainableParam`、Express loss、`SGD::step`を使用しています。
- Train固有の「Android GPU backward backend」があるわけではありません。勾配定義が
  Expressの通常演算graphを生成し、選択Executorがその演算を配置します。

### 対象演算のbackward

| forward | source確認 | backward graph |
|---|---|---|
| MatMul | [`MatMulGrad.cpp`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/tools/train/source/grad/MatMulGrad.cpp#L102-L232) | transpose flag付きMatMulでdA/dB |
| Sub | [`BinaryGrad.cpp`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/tools/train/source/grad/BinaryGrad.cpp#L94-L99) | dA=dOut、dB=-dOut |
| Square | [`UnaryGrad.cpp`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/tools/train/source/grad/UnaryGrad.cpp#L73-L78) | x*dOutを2回加算 |
| ReduceMean | [`ReduceGrad.cpp`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/tools/train/source/grad/ReduceGrad.cpp#L47-L66) | 要素数scale、unsqueeze、broadcast multiply |

勾配定義の存在は確認済みです。ただし、生成された補助演算を含むgraph全体が特定GPUで
実行できる保証ではありません。

### OpenCL / Vulkanの構成演算

- OpenCLはbuffer/image双方についてMatMul、Binary、Unary、Reduction creatorを
  [`OpenCLOPRegister.cpp`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/source/backend/opencl/core/OpenCLOPRegister.cpp#L7-L124)で登録します。
- Vulkan image backendは
  [MatMul](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/source/backend/vulkan/image/execution/VulkanMatMul.cpp#L416-L439)、
  [Binary](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/source/backend/vulkan/image/execution/VulkanBinary.cpp#L185-L193)、
  [Unary](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/source/backend/vulkan/image/execution/VulkanUnary.cpp#L139-L147)、
  [Reduction](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/source/backend/vulkan/image/execution/VulkanReduce.cpp#L131-L139)
  のcreatorを持ちます。

従って対象のforward演算と、backwardが生成する主要演算のGPU実装はソース上存在します。
形状、format、補助演算、端末driverを含むend-to-end成立性は実機ログで判定します。

### optimizer更新の実行先

MNN 3.5.0標準SGDは完全device-residentではありません。

1. [`SGD.cpp` L91-L104](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/tools/train/source/optimizer/SGD.cpp#L91-L104)で
   parameterとgradientを`prepareCompute`後に`readMap()`し、host `_Const`へ置換します。
2. update式自体はExpress graphとして作られるため、Sub/Mul等が選択backendで実行される
   可能性があります。
3. [`ParameterOptimizer.cpp` L132-L138](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/tools/train/source/optimizer/ParameterOptimizer.cpp#L132-L138)が
   updateをfixした後、parameterへ`input()`します。
4. [`Expr.cpp`のVariable::input`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/express/Expr.cpp#L626-L676)は
   sourceを`readMap()`してhost `memcpy`します。

したがってログに`optimizer_host_sync=true`を常に出します。「重み更新演算の一部がGPU」
と「optimizer stateがGPU常駐」は区別してください。

### CPUフォールバック

- [`ScheduleConfig::backupType`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/include/MNN/Interpreter.hpp#L63-L64)のdefaultはCPUです。
- [`Pipeline.cpp` L540-L575](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/source/core/Pipeline.cpp#L540-L575)は
  main backendでExecutionを作れなければbackup backendを試します。
- [`setGlobalExecutorConfig`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/express/Executor.cpp#L28-L45)も
  runtime作成失敗時にCPUへ切り替えます。

本アプリはsilentなglobal setterを使わず、`MNNGetExtraRuntimeCreator`と
`Executor::newExecutor`を検証します。それでもper-op backupは存在するためcallbackで検出し、
GPU runの`status`をFAILEDにします。

### Executorのthread scope

[`ExecutorScope.cpp`](https://github.com/alibaba/MNN/blob/c35f14f3ab5cb65094863b9a0e888370b027a670/express/ExecutorScope.cpp#L24-L68)は
Androidで`thread_local` scopeを使用します。scopeをUI threadで作ってworkerへ渡すと、workerは
global CPU Executorを使います。本アプリはJNI worker thread内でExecutor作成から学習終了まで
`ExecutorScope`を保持します。

### OpenCLとVulkanの同一APK

両オプションを同時にONにし、モノリシック`libMNN.so`へ含める構成はビルド成功しています。
これは「端末runtimeが利用可能」または「演算が実機で成功」を意味しません。起動時に
`runtime_available_opencl`と`runtime_available_vulkan`を別々にprobeします。

## QNN / QAIRT状況

2026-07-14時点でQAIRT 2.48.40.260702151143を監査し、Android AArch64の公式tool、CPU/HTP
backend、V81 stub、HTP Prepare、SampleApp、header/APIを確認済みです。QNN core APIは2.37.0、
HTP backend APIは5.48.0です。

- QNN CPUの最小MatMulとPhoneLM forward/graph再利用は成功。
- HTPはlibrary load、provider選択、logCreate、backendCreateまで成功。
- NULL-config deviceCreateはPhoneLM、公式SampleApp、独立最小再現の全てで14001。
- 公式qnn-net-run HTPもdevice creationで停止、CPU backendは成功。
- vendor signed V81 skelは存在するがshellからread/stat/pull不可。
- vendor QNN versionとQAIRT 2.48.40との正式な互換性は未確認。

SDK、vendor binary、APK、生ログ、ローカルSDK絶対パスはGitや公開support bundleへ含めません。

### 正式なSDK取得

QAIRT/QNN SDKやQualcomm binaryは自動取得せず、非公式mirrorも使用しません。

1. [Qualcomm Software Center](https://softwarecenter.qualcomm.com/)または
   [QPMの公式導入案内](https://docs.qualcomm.com/bundle/publicresource/topics/80-77512-1/hexagon-dsp-sdk-getting-started.html?product=1601111740010422)
   からQualcommの正式な配布経路へサインインします。
2. カタログで利用可能な`Qualcomm AI Runtime SDK`を選択し、対象Android/SoCに必要な
   package/add-onを取得します。表示される利用規約、export control、entitlementに従います。
3. SDKを任意のローカルdirectoryへ展開し、そのrootを本projectへ渡します。
4. [QAIRT/QNN公式documentation catalog](https://docs.qualcomm.com/bundle/publicresource/topics/80-63442-4/developing-apps-qualcomm-neural-processing-sdk.html?product=1601111740010412)
   と、インストールされた同一versionのsample/build fileを照合します。

Qualcomm OneID、規約同意、製品/SoCごとのentitlementが必要になる場合があります。この環境には
認証済みsessionもSDK archiveもないため、取得操作は自動化していません。

### SDK inventory script

PowerShell:

```powershell
.\scripts\check_qairt.ps1 -SdkRoot C:\path\to\qairt
```

macOS/Linux/WSL:

```sh
sh ./scripts/check_qairt.sh --sdk-root /path/to/qairt
```

path引数を省略した場合は、`QAIRT_SDK_ROOT`、`qairt.sdkRoot`、一般的な候補の順に探します。
scriptはSDK内を列挙して、固定basenameを仮定せず実際に見つかったpathを出力します。

- `QnnInterface.h` / `QnnTypes.h`
- header macroから判定できたQNN API version evidence
- ELF machineとpathで確認したAndroid arm64 `.so` directory
- 実ファイル名から抽出したCPU backend、HTP backend、HTP prepare相当候補
- DSP skel/stub候補
- `qnn-net-run` / `qnn-platform-validator`
- `QnnInterface.h`をincludeするSDK内sample候補

role分類は候補です。最終的なlink/package/deploy対象は、必ず同じSDK versionの公式sample build
fileで確定します。script成功だけでは`qnn_implementation_ready=true`になりません。

SDKが未設定のQNN OFF buildでは、従来どおり次のblocked出力を使用します。これは現在の
QAIRT導入済みQNN ON実機結果とは別のfail-safe経路です。

```text
sdk_root_exists=false
qnn_interface_header_exists=false
qnn_types_header_exists=false
qnn_implementation_ready=false
status=BLOCKED_BY_QAIRT_SDK_NOT_INSTALLED
```

アプリでQNN modeを押した場合も、成功値を埋めず次のように終了します。

```text
QNN_EXPERIMENT_RESULT
qnn_sdk_detected=false
qnn_implementation_ready=false
qnn_backend=HTP
qnn_backend_initialized=false
qnn_graph_finalized=false
npu_forward_used=false
npu_dw_used=false
status=BLOCKED_BY_QAIRT_SDK_NOT_INSTALLED
error=QNN_SDK_NOT_FOUND: cannot initialize HTP
NPU_TRAINING_RESULT
steps_completed=0
npu_forward_steps=0
npu_backward_dw_steps=0
status=BLOCKED_BY_QAIRT_SDK_NOT_INSTALLED
```

### 任意ビルド設定

default:

```text
PHONELM_ENABLE_QNN=OFF
QAIRT_SDK_ROOT=
```

Gradle propertyは次の形を予約しています。

```powershell
.\gradlew.bat :app:assembleDebug `
  "-Pphonelm.enableQnn=true" `
  "-Pqairt.sdkRoot=C:\path\to\installed\qairt"
```

QNN OFFでは`qnn_runtime_stub.cpp`だけを選択し、QAIRT header/libraryをcompile/link/package
しません。2026-07-11のDebug `libphonelm_native.so`はQAIRT/QNNの`DT_NEEDED`と未解決symbolが
ともに0件でした。QNN ONではstubを絶対に選択しません。

- SDK headerがなければCMakeはSDK未導入として失敗します。
- SDK headerが見つかったQNN ON buildでは監査済み`qnn_runtime_qairt.cpp`だけをcompileし、
  stubへフォールバックしません。
- Android arm64のCPU/HTP/HTP Prepare/V81 stubはローカルSDKからbuild時にstageし、Gitには
  追加しません。

これはstubをQNN-enabled APKへ混入させないための意図的なfail-fastです。DSP側はvendor signed
V81 skelを使用し、vendor領域の変更やSDK unsigned skelの配置は行っていません。

### SDK検出値

`scripts/check_qairt.*`の出力とSDK sampleから、次をREADMEへ確定値として追記します。

```text
qairt_sdk_version=...
qnn_api_version=...
sdk_root=...
android_arm64_include_directory=...
android_arm64_library_directory=...
htp_runtime_library_directory=...
dsp_library_directory=...
official_android_sample=...
official_cpu_backend_sample=...
official_htp_backend_sample=...
```

QAIRT 2.48.40.260702151143、QNN core API 2.37.0、HTP backend API 5.48.0、Android
AArch64 backend/stub/prepareの実ファイルを確認済みです。ローカル絶対パスは公開資料へ含めません。

### HTP初期化成立後に再開する順序

1. Qualcomm/nubiaからNX741J firmwareに対応するQAIRT/QNN版と第三者HTPアクセス条件を得る。
2. 同じNULL-config device-only probeでdeviceCreate成功を確認する。
3. 成功した場合だけHTP forwardをCPU基準と比較する。
4. その後に限り`transpose(X) * dP`の`dW`、CPU master weight/SGDへ進む。

QNN CPU成功をHTP成功として扱わず、backend初期化、graph finalize、executeの各errorを
API名、op、shape、dtype、quantizationとともに記録します。

## 達成状況

| 条件 | 状況 |
|---|---|
| Android Studio互換Kotlin/NDK project | 達成 |
| `arm64-v8a`のみ | 達成 |
| MNN固定版の再現取得 | 達成 |
| MNN Train/Express/OpenCL/Vulkanをcompile | 達成 |
| Debug APK生成 | 達成 |
| APKへMNN/native library同梱 | 達成 |
| pure C++ CPU forward/backward/SGD | 達成 |
| CPU gradient check | 達成 |
| SDK非依存量子化/shape/buffer/mock混成学習test | 達成（host） |
| Kotlin unit test | 達成（2026-07-11再確認） |
| 実機CPU reference | 達成 |
| 実機OpenCL/Vulkan | 本調査の対象外 |
| GPU per-op fallback結果 | 未取得（実機が必要） |
| QAIRT SDK inventory script | 達成 |
| QAIRT SDK検出 | 達成（2.48.40.260702151143） |
| QNN CPU forward | 達成、graph再利用確認済み |
| QNN HTP deviceCreate | 失敗（14001）、三経路で独立再現 |
| QNN HTP forward/dW | deviceCreate未成立のため未実施 |
| NPU勾配によるloss低下 | 未達 |

## 既知の制約

- MNN標準SGDは各stepでhost同期します。
- callbackによるbackend追跡は固定MNN内部APIを使うbest-effort診断です。
- GPU timestampではなくwall clockを計測します。`readMap`同期時間もstep時間に含みます。
- loss reportのためのforwardとoptimizerのgraph構築コストを含みます。
- Stopは実行中kernelを強制中断せず、step境界で処理します。
- OpenCL vendor library可視性はAndroid linker namespaceと端末firmwareに依存します。
  Manifestには`libOpenCL.so`を`required=false`で宣言しています。
- QAIRT SDKやQualcomm binaryをリポジトリへ含めていません。

## MNN GPUが成立しない場合の次候補

| 方針 | 利点 | コスト/注意 |
|---|---|---|
| 自作Vulkan Compute + 独自autodiff | forward/backward/update配置を完全制御 | shader、allocator、同期、数値検証の実装量が大きい |
| QAIRT/QNN explicit graph | HTPへforward/dWを明示配置可能な研究経路 | SDK/端末対応型/量子化/配布条件をversionごとに確認必須 |
| CPU master weight + GPU/NPU演算分割 | 1演算ずつ移行しやすく、誤差を隔離可能 | 転送と同期が多く速度は期待しない |
| 別training runtime | optimizer/autodiffを再利用できる可能性 | Android GPU backwardと対象演算を別途source/実機検証必須 |

## 次の1ステップ

現在の優先作業はTransformerではありません。

1. `support/qualcomm-qnn-htp-devicecreate-report.md`と公開bundleをQualcommへ提出する。
2. 短縮版`support/nubia-qnn-htp-devicecreate-report.md`をnubiaへ提出し、firmware/runtime担当への転送を依頼する。
3. 対応QAIRT/QNN版、host/stub/skel interface、第三者HTPアクセス条件の回答を待つ。
4. 根拠ある回答が得られるまで追加version試験、device config変更、FP16、量子化、dWへ進まない。

HTPで生成したdWによるloss低下まで確認した後にのみ、Transformer 1 blockへの拡張を
検討します。
