# PhoneLM: MNN GPU training / QNN HTP experiment probe

Androidアプリ内で小規模な線形回帰を学習し、MNN CPU・OpenCL・Vulkanの成立性と、
将来のQAIRT/QNN HTP学習演算を段階的に検証するための最小プロジェクトです。

このREADMEは「ビルドできたこと」と「実機で成立したこと」を区別します。

## 現在の結論

- Android Debug APKは `arm64-v8a` 向けにビルド成功しています。
- 独立したC++ CPU基準実装は、forward、MSE、`dW`、`dX`、SGD、数値微分、
  100 stepのloss低下をホストで検証済みです。
- MNN 3.5.0のExpress/MNN-Train、OpenCL、Vulkanは同一の`libMNN.so`へ組み込み済みです。
- nubia Z80 Ultra実機はこの開発環境へ接続されていないため、MNN CPU/OpenCL/Vulkanの
  実行結果は未取得です。GPU成功を主張していません。
- MNN 3.5.0標準`SGD::step`は勾配とパラメータを`readMap()`でホストへ同期し、
  `Variable::input()`でもホストコピーします。そのため、標準SGDを用いた今回の経路は
  「完全GPU常駐のoptimizer」ではありません。GPU上で実行された演算は個別に記録します。
- QAIRT/QNN SDKはローカル環境に見つかりませんでした。QNN APIやライブラリ名は推測せず、
  抽象層、実行モード、SDK検出、`QNN_SDK_NOT_FOUND`レポートまで実装しています。

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
./scripts/fetch_mnn.sh
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
$env:JAVA_HOME="C:\Users\yuubi\scoop\apps\temurin17-jdk\current"
$env:ANDROID_HOME="C:\Users\yuubi\AppData\Local\Android\Sdk"
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
| `app-debug.apk`（debug署名済み） | 159,254,721 bytes | `D405CF0E228CD4CD1E17458C5CB1C5472ADDF5314D0545BDC485CDCB4ADA5203` |
| `app-release-unsigned.apk` | 3,412,386 bytes | `58BFD71B0A6846561763C1E0606CBC9A56EEF6B0B87C8B5C0E582AF294D9286B` |
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
    qnn_linear_training.cpp/.h
    qnn_runtime.cpp/.h
    qnn_tensor.cpp/.h

host_tests/
  cpu_reference_training_test.cpp

scripts/
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
  SDK-independent interface and explicit NOT_IMPLEMENTED result
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

QNN modeはSDK未導入の現在、必ず`QNN_SDK_NOT_FOUND`か`NOT_IMPLEMENTED`を返し、
成功表示にはなりません。

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

2026-07-10に次をローカルで確認しました。

- `QAIRT_SDK_ROOT`、QNN、Qualcomm、Hexagon、SNPE関連環境変数: なし
- `QnnInterface.h`、`QnnTypes.h`: なし
- `qnn-net-run`、`qnn-platform-validator`、`snpe-net-run`: なし
- 標準配置、ユーザーDesktop/Documents/Downloads、Dドライブ: SDKなし
- 接続実機: なし（offline emulatorのみ）

このため、SDK version、QNN API version、backend library、HTP dtype、Transpose、MatMul、
ReduceMean、共有buffer、DSP library配置はすべて`NOT_AVAILABLE`または`UNVERIFIED`です。

### 任意ビルド設定

default:

```text
PHONELM_ENABLE_QNN=OFF
QAIRT_SDK_ROOT=
```

Gradle propertyは次の形を予約しています。

```powershell
.\gradlew.bat :app:assembleDebug `
  -Pphonelm.enableQnn=true `
  -Pqairt.sdkRoot=C:\path\to\installed\qairt
```

`QnnInterface.h`を検出できない状態でONにするとCMakeは明示的に失敗します。現時点の
QNN adapterはSDK非依存stubであり、SDKを検出しても`implementation_ready=false`です。
実ヘッダーとそのversionの公式sampleを確認するまで、QNN symbolを追加しません。

### SDK導入後に実装する順序

1. SDK version/API versionを実ファイルから記録。
2. SDK同梱のAndroid arm64 sampleで、graph create/add/finalize/executeとruntime tensorを確認。
3. 公式sampleが指定するruntime/DSP libraryだけをCMake/APKへ追加。
4. QNN CPU backendでB=2、D=4の`P=XW`。
5. 同じgraphを再利用し、runtime input Wを変更。
6. HTPでforward。CPUとの差をmax/mean absolute、max relativeで記録。
7. HTPで`transpose(X) * dP`を明示graph化し、`dW`をCPU基準と比較。
8. HTP `dW`、CPU master weight/SGDで20 step以上学習。
9. 成功後だけdX、loss、dP、updateを順に移動。

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
| Kotlin unit test | 達成 |
| 実機CPU 100 step | 未実施（端末未接続） |
| 実機OpenCL/Vulkan | 未実施（端末未接続） |
| GPU per-op fallback結果 | 未取得（実機が必要） |
| QAIRT SDK検出 | 未達（SDKなし） |
| QNN CPU forward/dW | 未実装（SDKなし） |
| QNN HTP forward/dW | 未実装（SDKなし） |
| NPU勾配によるloss低下 | 未実装（SDKなし） |

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

1. nubia Z80 UltraへDebug APKをinstall。
2. MNN CPU Smallを100 step実行し、loss低下を確認。
3. OpenCL/Vulkanを同一seedで実行し、`executed_backends`とfallback opを取得。
4. QAIRT SDKをローカルへ導入し、version、header、公式Android sampleを提供。
5. QNN CPU B=2/D=4 forwardを実装・比較。
6. HTP forward、その後HTP dWへ1段ずつ進む。

HTPで生成したdWによるloss低下まで確認した後にのみ、Transformer 1 blockへの拡張を
検討します。
