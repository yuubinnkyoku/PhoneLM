# QNN HTP fused backward

## Scope

QAIRT 2.48.40.260702151143 (QNN Core API 2.37.0, HTP Backend API 5.48.0) and the V81 Stub/Skel are used. No QAIRT 2.47 component is accepted by the build or APK audit.

The two-layer bias-free network is `Z1=XW1`, `H=ReLU(Z1)`, `P=HW2`. The CPU computes MSE loss, `dP=2(P-Y)/(B*O)`, SGD, and the app-owned W1/W2 updates. QNN does not provide automatic differentiation here: every backward expression is an explicitly constructed graph.

## Split and fused graphs

The retained split baseline executes three backward graphs per step: `dW2=H^T dP`, `dH=dP W2^T`, CPU `dZ1`, and `dW1=X^T dZ1`. The fused mode uses one backward graph and one execute per step:

```text
                  +--> MatMul(H^T,dP) -----------------> dW2
X,H,dP,W2,zero ---+
                  +--> MatMul(dP,W2^T) --> dH --+
                  H --> Greater(H,zero) --> mask +--> Select(mask,dH,zero) --> dZ1
                  X -----------------------------------------------------------> MatMul(X^T,dZ1) --> dW1
```

`QNN_OP_MAT_MUL_PARAM_TRANSPOSE_IN0=true` is used for dW2 and dW1. `QNN_OP_MAT_MUL_PARAM_TRANSPOSE_IN1=true` is used for dH.

QAIRT 2.48's `QnnOpDef.h` has no dedicated ReLU gradient op. The adopted implementation is `ElementWiseGreater` followed by `ElementWiseSelect`. H/dH/dZ1 are FLOAT_32 and the mask is BOOL_8. `H > 0` produces true; zero, negative zero, and negative values produce false. A standalone `[2,5]` graph checks negative, zero, `+-1e-6`, and positive inputs against the CPU rule.

In correctness mode dH, mask, and dZ1 are APP_READ diagnostic outputs in addition to dW2/dW1. In benchmark mode they are NATIVE intermediates and only dW2/dW1 are returned, avoiding diagnostic synchronization and copy overhead.

## Lifecycle and buffers

The fused run creates/finalizes one forward graph and one backward graph. They share the existing backend, device, and context and are reused for all steps. Expected counts are forward execute `steps+1` (including the updated-weight check) and fused backward execute `steps`.

The CPU canonical W1/W2 vectors are app-owned. `setMlpWeights` copies both once after every SGD update. Forward binds W1/W2 from those vectors and fused backward binds the same current W2 vector. Backward always runs before SGD, so dH uses the W2 that generated that step's forward output. Only after dW1/dW2 have returned are both vectors updated for the next step.

## Correctness, fallback, and benchmarking

`QNN_HTP_RELU_BACKWARD_CHECK` validates the standalone graph. `QNN_HTP_MLP_FUSED_BACKWARD` enables diagnostic outputs and CPU analytic comparisons. `QNN_HTP_MLP_FUSED_BACKWARD_BENCHMARK` removes per-step CPU references and diagnostic outputs while retaining loss, dP, optimizer, finite checks, and explicit failure checks.

The fused mode never falls back to CPU ReLU or linear backward. Graph create/finalize/execute, missing output, or weight update failure ends the run as FAILED. `cpu_fallback=false`, `htp_relu_backward_used=true`, and `htp_fused_backward_used=true` are required. Loss, dP, SGD, and weight updates remain CPU operations; this is not NPU-only training.

Performance is reported separately for fused HTP backward, full step, and total runs. Split/fused order alternates by repetition. Battery temperature, Android thermal status, battery level, and charging status are sampled; a run is not started at 45 C or above. Debug scalar CPU numbers are not representative of an optimized device CPU implementation. Initialization-inclusive break-even is an estimate and must be labeled as such when graph-only initialization timing is unavailable.

## Reproduction

```powershell
.\scripts\run_qnn_htp_fused_backward_tests.ps1 `
  -QairtSdkRoot 'C:\Qualcomm\AIStack\QAIRT\2.48.40.260702' `
  -ExpectedBuildId '2.48.40.260702151143' `
  -Repetitions 3 `
  -Seeds 20260710,20260711,20260712,20260713,20260714 `
  -RunPerformance
```

The script audits the APK, rejects non-unique online ADB connections without persisting the endpoint, supports resuming successful run files, applies timeouts and thermal guards, and writes reports under `build/reports`. APKs, logs, CSV/JSON reports, and Qualcomm binaries are excluded from Git.