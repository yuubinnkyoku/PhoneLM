# QAIRT公式ツール用の最小MatMulモデル

## モデル

固定shape、FP32、単一MatMulで、`X [2,4]` と `W [4,4]` は両方とも入力、`P [2,4]` が出力である。Wはinitializerへ埋め込んでいない。入力は NumPy `default_rng(seed=20260710)` の一様分布 `[-0.25, 0.25]` から生成した。

生成物はGit対象外の `build/qnn_probe/` に置く。

- `minimal_matmul.onnx`
- `converter/minimal_matmul.cpp`, `converter/minimal_matmul_net.json`
- `model_lib_build/libs/arm64-v8a/libminimal_matmul.so`
- `inputs/X.raw`, `inputs/W.raw`, `input_list.txt`
- `cpu_reference/P.raw`, `cpu_reference/P.txt`

## 生成コマンド

QAIRTがWindowsでサポートするCPython 3.10環境を使用し、`sdk.yaml` と同じ ONNX 1.16.1 / ONNX Runtime 1.17.1 を入れた。QAIRT自体の版は変更していない。

```powershell
python build/qnn_probe/generate_minimal_matmul.py

& "$env:QAIRT_SDK_ROOT\bin\x86_64-windows-msvc\qnn-onnx-converter" `
  --input_network build\qnn_probe\minimal_matmul.onnx `
  --output_path build\qnn_probe\converter\minimal_matmul.cpp `
  --float_bitwidth 32
```

converterは `32 MACs`、`Total params = 0` と報告した。Wが入力なので定数binは生成されない。

Android model libraryはSDK同梱の `share/QNN/converter/jni`（Android.mk、Application.mk、公式wrapper）をそのまま使用し、SDK指定のAndroid NDK r26cで生成した。

```powershell
$env:QNN_SDK_ROOT = $env:QAIRT_SDK_ROOT
$env:QNN_ANDROID_APP_ABIS = "arm64-v8a"
$env:QNN_MODEL_LIB_NAME = "libminimal_matmul.so"
& "$env:LOCALAPPDATA\Android\Sdk\ndk\26.2.11394342\ndk-build.cmd" `
  -C build\qnn_probe\model_lib_build
```

全コマンドの実行記録は `build/qnn_probe/commands.txt`、公式CLIのhelpは `diagnostics/local/tool-help/` に保存する。

## CPU検証

同一model libraryを公式 `qnn-net-run` + `libQnnCpu.so` で実行し、`Result_0/P.raw` とNumPy基準値を比較した。結果は `max_abs_error=0.0`、完全一致だった。

HTPではSDKのbackend extension schemaに従い、`libQnnHtpNetRunExtensions.so` と `devices: [{"dsp_arch":"v81", "pd_session":"signed", "device_id":0}]` も使用した。SDKのunsigned V81 skelはproduction端末へ配置せず、既存vendor signed skelの探索先を `ADSP_LIBRARY_PATH` に含めた。
