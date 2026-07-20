# QAIRT 2.48 HTP weight-gradient MatMul

PhoneLMの線形回帰では、forwardと、backwardの一部である重み勾配`dW = X^T dP`計算をQAIRT 2.48のQNN HTPで実行する。loss、`dP`生成、SGD optimizer、重み更新はCPUで行う。backward全体、loss、optimizerをHTPで実行する構成ではない。

## 処理分担

```text
HTP: P = X W
CPU: loss = mean((P - Y)^2)
CPU: dP = 2(P - Y) / (B O)
HTP: dW = X^T dP
CPU: W = W - learning_rate dW
QNN: 更新済みAPP_WRITE Wでforward graphを再実行
```

QNNは自動微分を行わない。forwardとは別の通常のQNN graphとして重み勾配式を明示的に構築する。現在は1層線形回帰なので`dX = dP W^T`を必要としない。将来2層MLPへ拡張するときは、前段へ勾配を渡すため`dX` graphが必要になる。

## TensorとTranspose

全tensorはdense FP32、row-major、app-owned RAW client bufferである。

```text
X:  [B,I]
W:  [I,O]
P:  [B,O]
dP: [B,O]
dW: [I,O]
```

QAIRT 2.48のQNN MatMulに`QNN_OP_MAT_MUL_PARAM_TRANSPOSE_IN0=true`を設定し、`X [B,I]`を転置入力として扱う。CPUで`X^T` bufferを生成しない。したがって`x_transpose_time`は0であり、transpose処理は`dw_graph_execute_time`に含まれる。`X`と`dP`は毎step client bufferをbindし直すが、tensor登録やgraph作成は繰り返さない。

## ライフサイクル

1個の`Runtime`がbackend、device、contextを作成する。同じcontext内に次の独立graphを作る。

```text
phonelm_matmul:    P  = X W
phonelm_dw_matmul: dW = transpose(X) dP
```

各runで両graphを1回create、1回finalizeし、全stepで再利用する。forwardの`W`は`QNN_TENSOR_TYPE_APP_WRITE`であり、CPU SGD後に既存client bufferへcopyして次のexecuteへ反映する。dW graphの`X`と`dP`もAPP_WRITE、`dW`はAPP_READである。新しいHTP dWモードでは、いずれかのQNN API失敗時に即時FAILEDを返し、CPU dWへfallbackしない。

基本640 step runでは、最終重み反映確認を含め次を期待する。

```text
forward_graph_create_count=1
forward_graph_finalize_count=1
dw_graph_create_count=1
dw_graph_finalize_count=1
forward_execute_count=641
dw_execute_count=640
runtime_weight_update_count=640
x_input_update_count=640
dp_input_update_count=640
```

backend、device、contextはforward/dW間で共有し、graphだけを分離する。

## 実行モード

- `QNN_HTP_DW_CHECK`: 小shapeを含む詳細correctness。CPU dWとHTP dWを比較する。
- `QNN_HTP_FORWARD_HTP_DW_TRAINING`: HTP forward + HTP dWのcorrectness training。CPU参照を毎step実行する。
- `QNN_HTP_FORWARD_HTP_DW_BENCHMARK`: HTP forward + HTP dWのbenchmark。CPU dW参照は初回、最終、指定intervalだけであり、HTP dW timingへ混ぜない。
- `QNN_HTP_TRAINING_BENCHMARK`: 比較基準のHTP forward + CPU dW benchmark。

correctnessはdW誤差、更新後forward、loss/weight trajectoryを検査する。benchmarkもloss、dP、SGD、有限性、fallback guardを維持するが、毎stepのCPU参照MatMul、文字列化、ファイルI/Oを測定経路から除く。

## 計測

結果にはforward、CPU dP、CPU dWまたはHTP dW execute、dW input/output bind、optimizer、APP_WRITE weight copy/update、full stepのmin/median/mean/stddev/p90/p95/maxを含める。graph内transposeなので独立したCPU transpose/copyはない。固定費を含む比較では、dW MatMul単体、dW bind/copy込み経路、full step、初期化込みtotal runを区別する。

## 再現

正式構成はQAIRT `2.48.40.260702`、Build ID `2.48.40.260702151143`、QNN Core API `2.37.0`、HTP Backend API `5.48.0`、Android NDK `26.2.11394342`、V81 Stub/unsigned Skelである。

```powershell
.\scripts\run_qnn_htp_backward_tests.ps1 `
  -QairtSdkRoot 'C:\Qualcomm\AIStack\QAIRT\2.48.40.260702' `
  -ExpectedBuildId '2.48.40.260702151143' `
  -Repetitions 3 `
  -Seeds 20260710,20260711,20260712,20260713,20260714 `
  -RunPerformance `
  -RunBreakEven
```

scriptは一意のonline ADB接続を要求するが、endpointやserialをレポートへ保存しない。長時間phase前と各run前後に温度・thermal statusを記録し、45℃以上では停止する。成功済み`runs/<name>/result.txt`がある同じ`-ReportRoot`を指定すれば再開できる。APK、CSV、JSON、ログ、Qualcomm配布バイナリは`build/`以下に生成しGitへ追加しない。