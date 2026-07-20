# QAIRT 2.48 QNN HTP 2層ReLU MLP学習

## 範囲

対象はbiasなしfloat32 MLPです。

```text
Z1 = X W1
H  = ReLU(Z1)
P  = H W2
loss = mean((P - Y)^2)
```

backwardは明示式で構築し、QNNの自動微分は使用しません。

```text
dP  = 2(P-Y)/(B*O)
dW2 = H^T dP
dH  = dP W2^T
dZ1 = dH * (H > 0)
dW1 = X^T dZ1
```

HTPはforward、dW2、dH、dW1を担当します。loss、dP、ReLU backward、SGD optimizerはCPUです。「backward全体をNPUで実行」または「NPUだけで学習」とは表現しません。

## datasetと初期化

固定seedの`mt19937_64`から`X ~ U(-0.5, 0.5)`を生成し、teacherで`Y = ReLU(X W1*) W2*`を作ります。W1はHe scale `sqrt(2/I)`、W2は`sqrt(2/(H+O))`の正規分布です。studentは同じscaleの別sampleです。CPU/HTPのdataset、student初期値、循環mini-batch順序は一致します。

CPU候補`0.05, 0.1, 0.2, 0.5, 1.0`を基本shapeで10 epoch検査しました。すべて有限で、保守的に`learning_rate=0.5`を採用しました。既存線形回帰の5は流用していません。

## QNN graph

同一backend/device/context内に次の4 graphをrunごとに1回だけcreate/finalizeします。

1. forward: `X,W1 -> MatMul -> Z1 -> Relu -> H; H,W2 -> MatMul -> P`
2. dW2: `H,dP -> MatMul(transpose_in0=true) -> dW2`
3. dH: `dP,W2 -> MatMul(transpose_in1=true) -> dH`
4. dW1: `X,dZ1 -> MatMul(transpose_in0=true) -> dW1`

正当性優先でdW2とdHは別graphです。このためstepあたりHTP executeが1回増え、約3.4ms級の固定費が加わります。HとPは`APP_READ` outputとして同じforward executeから取得し、Z1は`NATIVE` intermediateです。QNN Reluのfinalize/executeが失敗した場合はCPU ReLUへfallbackしません。

W1/W2はapp-owned vectorへ初回bindし、その後は同じ容量へcopyします。forwardとdH graphは同じW2 bufferを同期実行時にbindするので、更新前W2でdW2/dHを計算後、CPU optimizer、1回のW2 copy、次stepという順序です。graphごとのtensor登録は別ですが、client bufferは共有します。

## correctnessとbenchmark

Correctness modeはCPU forward/linear gradient参照、全tensor有限性、更新後forwardを確認します。Benchmark modeはstep内CPU参照MatMulを除外し、実際に必要なloss/dP、CPU ReLU backward、optimizer、weight update、fallback/有限性検査だけを残します。各区間はmin/median/mean/stddev/p90/p95/maxを出力します。

有限差分はB=2,I=4,H=5,O=3、epsilon=1e-3、ReLU 0近傍閾値5e-3です。HTP dX単体はB=2,I=4,O=3で`QNN_OP_MAT_MUL_PARAM_TRANSPOSE_IN1=true`を使用し、最大絶対誤差1e-4未満を要求します。

## 制約

- QAIRT `2.48.40.260702151143`、QNN Core 2.37.0、HTP Backend 5.48.0、V81 Stub/Skelのみを使用します。
- HTP失敗をCPU成功で置換しません。
- CPU基準はscalar C++とdebug APKであり、端末CPUの最適性能ではありません。
- DVFSは通常動作、周波数固定なし、45°C thermal guardありです。
- 2層ReLUはhidden unitの置換・scale非一意性があるためteacher weight errorは必須成功条件にしません。dataset loss、prediction、CPU/HTP trajectoryを主指標にします。

## 再現

```powershell
.\scripts\run_qnn_htp_mlp_tests.ps1 `
  -QairtSdkRoot 'C:\Qualcomm\AIStack\QAIRT\2.48.40.260702' `
  -ExpectedBuildId '2.48.40.260702151143' `
  -Repetitions 3 `
  -Seeds 20260710,20260711,20260712,20260713,20260714 `
  -RunPerformance
```

結果は`build/reports/qnn-htp-mlp-<timestamp>/`へ出力され、Gitへ追加されません。ADB endpointやserialは保存しません。