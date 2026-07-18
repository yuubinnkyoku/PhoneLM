# QNN HTP forward + CPU backward 線形回帰学習

## 構成

PhoneLMの線形回帰学習は `Y = XW*`（biasなし）を使用する。入力、真値重み、初期重みは固定seedの`std::mt19937`からfloat32で生成し、CPU版とHTP版で同じ値を再生成する。既定の基本試験は`batch=8, input_dim=128, output_dim=128, steps=100`である。

HTPが担当する処理は、QAIRT 2.48の`libQnnHtp.so`でfinalizeしたMatMul forwardだけである。loss、`dP`、`dW`、SGD optimizer、重み更新はCPUで行う。HTP上でbackwardまたはoptimizerを実行していない。

重みtensorはinitializerではなく`QNN_TENSOR_TYPE_APP_WRITE`のgraph入力である。graph作成とfinalizeはループ外で各1回だけ行い、CPUで更新したfloat32重みバッファを次の`QnnGraph_execute`へ渡す。PhoneLMの結果中の`runtime_weight_update`はこの既存graphのAPP_WRITE重み入力更新を指し、graphの再生成を意味しない。

```text
QNN HTP MatMul forward
  -> CPU MSE loss
  -> CPU dP = 2(P-Y)/(batch*output_dim)
  -> CPU dW = transpose(X)*dP
  -> CPU SGD: W = W - learning_rate*dW
  -> APP_WRITE weight buffer update
  -> 同じHTP graphで次step
```

最終optimizer更新後に追加forwardを1回実行する。このため`steps=100`では`graph_create_count=1`、`graph_finalize_count=1`、`runtime_weight_update_count=100`、`graph_execute_count=101`になる。追加executeは最終重みの反映とCPU参照出力との一致を検証するためである。

## 正当性と収束

lossは全`batch * output_dim`要素の平均二乗誤差である。行列はrow-major、tensorはrank 2のfloat32 RAW bufferとして対応する。`QNN_LINEAR_GRADIENT_CHECK`は`batch=2, dim=4`で解析勾配を中央有限差分と比較する。

HTP学習は次をすべて満たさなければ失敗する。

- HTP backend、device、context、graph create/finalize/executeが成功する
- QAIRT compile-time Build IDとruntime backend Build IDが一致する
- graph作成とfinalizeが各1回である
- runtime weight updateが各stepで成功する
- 次のHTP出力が変化し、更新後重みに対するCPU MatMulと`1e-3`以内（学習中の累積誤差用。単発MatMul回帰は従来どおり`1e-5`）で一致する
- lossと真値重みに対するRMS errorが低下する
- NaN/Infがない
- `npu_forward_used=true`かつ`cpu_fallback=false`である

`batch < input_dim`の基本問題は劣決定であり、真値重みのnull-space成分をデータから同定できない。このため`final_loss <= initial_loss * 0.1`を一律の合否条件にはせず、loss低下、weight error低下、CPU基準との収束傾向、更新反映を必須条件にしている。

## 対応環境

- QAIRT: `2.48.40.260702151143`
- QNN Core API: `2.37.0`
- HTP Backend API: `5.48.0`
- HTP architecture: V81
- Android NDK: `26.2.11394342`
- CMake: `3.22.1`

SDK header、Android runtime、V81 Stub、unsigned V81 Skelは同じQAIRT配布物でなければビルド・実行を拒否する。HTPモードでCPU backendへ切り替えるfallbackはない。

## 実行

```powershell
.\scripts\run_qnn_training_tests.ps1 `
  -QairtSdkRoot 'C:\Qualcomm\AIStack\QAIRT\2.48.40.260702' `
  -ExpectedBuildId '2.48.40.260702151143' `
  -BatchSize 8 `
  -InputDim 128 `
  -Steps 100 `
  -LearningRate 5
```

`-RunLarge`を付けると、基本試験の成功後に`batch=32, dim=512, steps=200`を実行する。スクリプトはgradient check、小shape CPU/HTP、基本shape CPU/HTP、CPU/HTP比較、任意の拡大試験、最後に既存QAIRT 2.48回帰試験を実行する。オンラインADB接続が一意でなければ停止し、レポートにendpointを保存しない。

結果は`build/reports/qnn-training-<timestamp>/`へ保存する。各runには`summary.json`、`loss.csv`、`timings.csv`、`device-test-result.txt`、`logcat.txt`がある。性能値はmonotonic clockで測り、first executeをsteady-stateから分離する。小shapeでは初期化、DSP通信、weight buffer更新の固定費によりHTPがCPUより遅い可能性がある。