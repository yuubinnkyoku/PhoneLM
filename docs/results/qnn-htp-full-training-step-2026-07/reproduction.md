# Reproduction

## 必要条件

- QAIRT `2.48.40.260702151143`をローカルに導入済みであること。
- Android NDK r26c (`26.2.11394342`)。
- CMake `3.22.1`。
- `arm64-v8a` Android端末。
- 対応するV81 HTP環境。
- ADBで対象端末へ接続できること。
- PowerShell、JDK 17、Android SDKが利用できること。

QAIRT runtime、Stub、Skelはリポジトリに含まれません。ユーザー自身のローカルQAIRT SDKからビルド時に取得します。公開APKも提供しません。

## 実行

リポジトリrootから次を実行します。

```powershell
$QairtSdkRoot = $env:QAIRT_SDK_ROOT

.\scripts\run_qnn_htp_full_step_tests.ps1 `
  -QairtSdkRoot $QairtSdkRoot `
  -ExpectedBuildId '2.48.40.260702151143' `
  -Repetitions 3 `
  -Seeds 20260710,20260711,20260712,20260713,20260714 `
  -RunPerformance
```

runnerはSDK/Build ID検査、clean build、APK監査、端末実行、micro correctness、small full-step、5 seed、性能比較、既存回帰、ローカルレポート生成を行います。端末温度、DVFS、バックグラウンド負荷により性能値は変動します。

## 公開用結果の再生成

実機レポート生成後、許可列だけを抽出します。

```powershell
.\scripts\export_public_qnn_results.ps1 `
  -SourceReportDir '.\build\reports\qnn-htp-full-step-validation' `
  -OutputDir '.\docs\results\qnn-htp-full-training-step-2026-07'
```

exporterはsummary、correctness、seed、performance、trajectoryの整合性を確認し、絶対パス、端末内private path、private network address、秘密鍵形式、禁止バイナリ形式が生成先にないか検査します。欠落値は推測せず空欄にします。生ログ、raw tensor、raw weight、QAIRTバイナリはコピーしません。
