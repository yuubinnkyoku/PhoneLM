# PhoneLM: CPU基準実装からQNN HTPへ学習演算を段階的に移すAndroid実験

## nubia Z80 Ultra実機結果（2026-07-18、QAIRT 2.48.40）

対象はnubia NX741J、Android 16/API 36、SM8850（HTP V81）です。正式なQNN構成は
QAIRT `2.48.40.260702151143`、QNN Core API `2.37.0`、HTP Backend API `5.48.0`です。

| 項目 | 結果 |
|---|---|
| QNN HTP backend/device/context create | SUCCESS（すべて0） |
| 固定FP32 MatMul compose/finalize/execute | SUCCESS |
| graph再利用とruntime weight update後の再実行 | SUCCESS |
| CPU fallback | false |
| backward/optimizerのHTP実行 | 未確認。loss/gradient/updateはCPU側 |

QAIRT 2.47と2.48の両方で、対応するV81 Stubとunsigned V81 Skelを同じSDK配布物から
揃えた場合にHTP実行へ成功しました。版固有の回帰ではありません。端末既定のDSP探索経路
だけでは`QnnDevice_create=14001`が再現するため、PhoneLMはビルド時にローカルQAIRT SDKから
Skelを取り込み、実行時に`files/qnn-dsp/<完全Build ID>/`へSHA-256検証付きでatomic展開します。
その版別ディレクトリをプロセス限定`ADSP_LIBRARY_PATH`の先頭へ追加します。root、SELinux変更、
システム領域変更は不要です。

実機回帰試験はオンライン端末がちょうど1台のときだけ実行されます。ADB endpointはレポートへ
保存しません。

```powershell
.\scripts\run_qnn_device_tests.ps1 `
  -QairtSdkRoot 'C:\Qualcomm\AIStack\QAIRT\2.48.40.260702' `
  -ExpectedBuildId '2.48.40.260702151143'
```

この試験はSDK検査、clean build、APK監査、更新インストール、probe/forward反復、force-stop、
アプリ専用Skelコピーの破損復旧を実行し、CPU fallbackを失敗として扱います。詳細は
`docs/qnn-htp-qairt-2.48.md`を参照してください。

技術的なローカル同梱と、公開APK/GitHub Releaseでの再配布は別問題です。QAIRT runtime、Stub、
Skelの第三者再配布可否はライセンス未確認であり、公開可能とは結論づけません。SDKバイナリは
Gitへ追加せず、各開発者のローカルSDKからビルド時に取り込みます。
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
| NDK | 26.2.11394342（r26c、QAIRT 2.48 `sdk.yaml`準拠） |
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

QNN CPU modeと`QNN_HTP_FORWARD`はQAIRT 2.48.40で実機実行済みです。HTP forwardは
context作成、固定MatMul finalize/execute、runtime weight update後の再実行まで成功しています。

| mode | QNNへ移す処理 | CPUに残す処理 | 現在 |
|---|---|---|---|
| `QNN_CPU_FORWARD` | QNN CPU forward | 比較値 | SUCCESS、graph再利用確認済み |
| `QNN_HTP_FORWARD` | HTP forward | 比較値 | SUCCESS、graph再利用とruntime weight update確認済み |
| `QNN_HTP_FORWARD_CPU_BACKWARD` | HTP forward | loss、`dP`、`dW`、SGD | forwardはHTP、backward/updateはCPUで試験可能 |
| `QNN_HTP_FORWARD_DW` | HTP forward、`dW` | loss、`dP`、SGD | 今回の正式回帰対象外、成功未確認 |
| `QNN_HTP_FORWARD_DW_DX` | HTP forward、`dW`、`dX` | loss、`dP`、SGD | `NOT_IMPLEMENTED` |
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

正式構成はQAIRT `2.48.40.260702151143`です。QNN Core APIは`2.37.0`、HTP Backend APIは
`5.48.0`、対象はSM8850/V81です。

- QAIRT 2.47と2.48の両方でHTP初期化・固定MatMul実行に成功し、版固有回帰を否定した。
- host runtime、V81 Stub、unsigned V81 Skelは必ず同一SDKルートから取り込む。
- Skelは版別アプリ専用領域へ安全に展開し、そのパスをプロセス限定DSP探索の先頭へ置く。
- compile-time SDK Build IDと`QnnBackend_getBuildId`の実行時値を照合する。
- CPU fallbackは常に明示し、成功条件として扱わない。
- QAIRT SDK、Qualcomm binary、APK、生ログ、ローカルSDK絶対パスはGitや公開support bundleへ含めない。
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

これはstubをQNN-enabled APKへ混入させないための意図的なfail-fastです。DSP側は同じローカル
SDKのunsigned V81 SkelをAPK assetへ取り込み、版別アプリ専用領域へ展開します。vendor領域や
システム設定は変更しません。公開APKでの再配布可否は別途ライセンス確認が必要です。

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

### HTP検証の順序

1. SDK metadata/header/runtime/Stub/Skelの完全Build IDを検査する。
2. versioned app-private SkelをSHA-256検証後にDSP探索パスへ追加する。
3. device-only probeでbackend/device/context createを個別検証する。
4. 固定MatMulのfinalize/executeとruntime weight update後の再実行をCPU基準と比較する。
5. HTP backward/optimizerは実測後にのみ成功と表現する。

QNN CPU成功をHTP成功として扱わず、各API結果と`cpu_fallback=false`を記録します。
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
| QNN HTP backend/device/context create | 達成（すべて0） |
| QNN HTP forward | 達成、固定MatMul再実行とruntime weight update確認済み |
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
