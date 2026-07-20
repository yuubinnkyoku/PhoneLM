#include "qnn_mlp_training.h"
#include "../cpu_reference_training.h"
#include "qnn_runtime.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

namespace phonelm::qnn {
namespace {
using cpu::Matrix;
using Clock = std::chrono::steady_clock;
struct Data {
  Matrix x, y, tw1, tw2, w1, w2;
};
struct Pass {
  Matrix z1, h, p, dp, dw2, dh, dz1, dw1;
  float loss = 0;
};
float normal(std::mt19937_64 &g, float s) {
  std::normal_distribution<float> d(0, s);
  return d(g);
}
Data makeData(const TrainingConfig &c) {
  std::mt19937_64 g(c.seed);
  Data d{Matrix(c.sampleCount, c.dimension),
         Matrix(c.sampleCount, c.outputDimension),
         Matrix(c.dimension, c.hiddenDimension),
         Matrix(c.hiddenDimension, c.outputDimension),
         Matrix(c.dimension, c.hiddenDimension),
         Matrix(c.hiddenDimension, c.outputDimension)};
  std::uniform_real_distribution<float> u(-0.5f, 0.5f);
  for (float &v : d.x.values)
    v = u(g);
  for (float &v : d.tw1.values)
    v = normal(g, std::sqrt(2.0f / c.dimension));
  for (float &v : d.tw2.values)
    v = normal(g, std::sqrt(2.0f / (c.hiddenDimension + c.outputDimension)));
  for (float &v : d.w1.values)
    v = normal(g, std::sqrt(2.0f / c.dimension));
  for (float &v : d.w2.values)
    v = normal(g, std::sqrt(2.0f / (c.hiddenDimension + c.outputDimension)));
  auto z = cpu::matMul(d.x, d.tw1);
  for (float &v : z.values)
    v = std::max(0.0f, v);
  d.y = cpu::matMul(z, d.tw2);
  return d;
}
Pass pass(const Matrix &x, const Matrix &y, const Matrix &w1,
          const Matrix &w2) {
  Pass r{cpu::matMul(x, w1),          Matrix(x.rows, w1.columns),
         Matrix(x.rows, w2.columns),  Matrix(x.rows, w2.columns),
         Matrix(w2.rows, w2.columns), Matrix(x.rows, w2.rows),
         Matrix(x.rows, w1.columns),  Matrix(w1.rows, w1.columns)};
  for (size_t i = 0; i < r.z1.values.size(); ++i)
    r.h.values[i] = std::max(0.0f, r.z1.values[i]);
  r.p = cpu::matMul(r.h, w2);
  r.loss = cpu::meanSquaredError(r.p, y);
  float s = 2.0f / static_cast<float>(x.rows * y.columns);
  for (size_t i = 0; i < r.dp.values.size(); ++i)
    r.dp.values[i] = (r.p.values[i] - y.values[i]) * s;
  r.dw2 = cpu::matMul(cpu::transpose(r.h), r.dp);
  r.dh = cpu::matMul(r.dp, cpu::transpose(w2));
  for (size_t i = 0; i < r.dz1.values.size(); ++i)
    r.dz1.values[i] = r.h.values[i] > 0 ? r.dh.values[i] : 0;
  r.dw1 = cpu::matMul(cpu::transpose(x), r.dz1);
  return r;
}
bool finite(const Matrix &m) {
  return std::all_of(m.values.begin(), m.values.end(),
                     [](float v) { return std::isfinite(v); });
}
double maxAbs(const Matrix &a, const Matrix &b) {
  double v = 0;
  for (size_t i = 0; i < a.values.size(); ++i)
    v = std::max(v, std::abs(double(a.values[i]) - b.values[i]));
  return v;
}
double meanAbs(const Matrix &a, const Matrix &b) {
  double v = 0;
  for (size_t i = 0; i < a.values.size(); ++i)
    v += std::abs(double(a.values[i]) - b.values[i]);
  return v / a.values.size();
}
double maxRel(const Matrix &a, const Matrix &b) {
  double v = 0;
  for (size_t i = 0; i < a.values.size(); ++i) {
    double den = std::max(
        {1e-7, std::abs(double(a.values[i])), std::abs(double(b.values[i]))});
    v = std::max(v, std::abs(double(a.values[i]) - b.values[i]) / den);
  }
  return v;
}
Matrix batch(const Matrix &m, int start, int count) {
  Matrix o(count, m.columns);
  for (int r = 0; r < count; ++r)
    std::copy_n(m.values.data() + ((start + r) % m.rows) * m.columns, m.columns,
                o.values.data() + r * m.columns);
  return o;
}
std::string gradientCheck() {
  TrainingConfig c;
  c.batchSize = 2;
  c.sampleCount = 2;
  c.dimension = 4;
  c.hiddenDimension = 5;
  c.outputDimension = 3;
  c.seed = 20260710;
  auto d = makeData(c);
  auto a = pass(d.x, d.y, d.w1, d.w2);
  const float e = 1e-3f;
  double a1 = 0, r1 = 0, a2 = 0, r2 = 0;
  int skip = 0;
  for (float z : a.z1.values)
    if (std::abs(z) < 5 * e)
      ++skip;
  auto check = [&](Matrix &w, const Matrix &analytic, double &ma, double &mr) {
    for (size_t i = 0; i < w.values.size(); ++i) {
      float old = w.values[i];
      w.values[i] = old + e;
      float p = pass(d.x, d.y, d.w1, d.w2).loss;
      w.values[i] = old - e;
      float n = pass(d.x, d.y, d.w1, d.w2).loss;
      w.values[i] = old;
      double num = (p - n) / (2 * e), an = analytic.values[i],
             ab = std::abs(num - an),
             re = ab / std::max({1e-6, std::abs(num), std::abs(an)});
      ma = std::max(ma, ab);
      mr = std::max(mr, re);
    }
  };
  check(d.w1, a.dw1, a1, r1);
  check(d.w2, a.dw2, a2, r2);
  bool ok = a1 < 2e-4 && a2 < 2e-4;
  std::ostringstream s;
  s << std::setprecision(9)
    << "MLP_GRADIENT_CHECK\nexecution_mode=QNN_MLP_GRADIENT_CHECK\nepsilon="
    << e << "\nnear_relu_zero_threshold=" << 5 * e
    << "\nskipped_near_relu_zero_count=" << skip << "\ndw1_max_abs_error=" << a1
    << "\ndw1_max_relative_error=" << r1 << "\ndw2_max_abs_error=" << a2
    << "\ndw2_max_relative_error=" << r2
    << "\nstatus=" << (ok ? "SUCCESS" : "FAILED")
    << "\ncpu_fallback=false\nnan_detected=false\ninf_detected=false\nnan_inf="
       "false";
  return s.str();
}
std::string dxCheck(const TrainingConfig &) {
  Runtime rt;
  std::string e;
  if (!rt.initialize(QnnBackendKind::HTP, e))
    return "DX_CHECK\nexecution_mode=QNN_HTP_DX_CHECK\nstatus=FAILED\nfailed_"
           "api=initialize\nerror=" +
           e + "\ncpu_fallback=false";
  if (!rt.prepareInputGradientMatMul(2, 4, 3, e))
    return "DX_CHECK\nexecution_mode=QNN_HTP_DX_CHECK\nstatus=FAILED\nfailed_"
           "api=prepareInputGradientMatMul\nerror=" +
           e + "\ncpu_fallback=false";
  Matrix dp(2, 3), w(4, 3);
  for (size_t i = 0; i < dp.values.size(); ++i)
    dp.values[i] = float(i + 1) * .07f;
  for (size_t i = 0; i < w.values.size(); ++i)
    w.values[i] = float(int(i % 5) - 2) * .11f;
  Matrix ref = cpu::matMul(dp, cpu::transpose(w));
  std::vector<float> out;
  if (!rt.executeInputGradient(dp.values, w.values, out, e))
    return "DX_CHECK\nexecution_mode=QNN_HTP_DX_CHECK\nstatus=FAILED\nfailed_"
           "api=graphExecute\nerror=" +
           e + "\ncpu_fallback=false";
  Matrix got(2, 4, out);
  double ma = maxAbs(ref, got), me = meanAbs(ref, got), mr = maxRel(ref, got);
  bool ok = ma < 1e-4 && finite(got);
  std::ostringstream s;
  s << std::setprecision(9)
    << "DX_CHECK\nexecution_mode=QNN_HTP_DX_CHECK\nbatch_size=2\ninput_dim="
       "4\noutput_dim=3\ntranspose_in1=true\nmax_abs_error="
    << ma << "\nmean_abs_error=" << me << "\nmax_relative_error=" << mr
    << "\ngraph_create_result=0\ngraph_finalize_result=0\ngraph_execute_result="
       "0\ncpu_fallback=false\nnan_inf="
    << (!finite(got) ? "true" : "false")
    << "\nstatus=" << (ok ? "SUCCESS" : "FAILED") << "\n"
    << rt.diagnostics();
  return s.str();
}
std::string reluBackwardCheck() {
  Runtime rt;
  std::string e;
  const std::string prefix =
      "RELU_BACKWARD_CHECK\nexecution_mode=QNN_HTP_RELU_BACKWARD_CHECK\n";
  if (!rt.initialize(QnnBackendKind::HTP, e))
    return prefix + "status=FAILED\nfailed_api=initialize\nerror=" + e +
           "\ncpu_fallback=false";
  if (!rt.prepareReluBackward(2, 5, e))
    return prefix + "status=FAILED\nfailed_api=prepareReluBackward\nerror=" +
           e + "\ncpu_fallback=false\n" + rt.diagnostics();
  Matrix activation(2, 5), dh(2, 5), ref(2, 5);
  activation.values = {-1.0f, -0.1f, -1e-6f, 0.0f, 1e-6f,
                       0.1f,  1.0f,  -0.0f, -1e-4f, 1e-4f};
  dh.values = {0.25f, -0.5f, 0.75f, -1.0f, 1.25f,
               -1.5f, 1.75f, -2.0f, 2.25f, -2.5f};
  for (size_t k = 0; k < ref.values.size(); ++k)
    ref.values[k] = activation.values[k] > 0.0f ? dh.values[k] : 0.0f;
  std::vector<std::uint8_t> mask;
  std::vector<float> output;
  if (!rt.executeReluBackward(activation.values, dh.values, mask, output, e))
    return prefix + "status=FAILED\nfailed_api=graphExecute\nerror=" + e +
           "\ncpu_fallback=false\n" + rt.diagnostics();
  Matrix got(2, 5, output);
  bool maskMatches = mask.size() == ref.values.size();
  for (size_t k = 0; maskMatches && k < mask.size(); ++k)
    maskMatches = (mask[k] != 0) == (activation.values[k] > 0.0f);
  const double ma = maxAbs(ref, got), me = meanAbs(ref, got),
               mr = maxRel(ref, got);
  const bool ok = ma <= 1e-7 && maskMatches && finite(got);
  std::ostringstream out;
  out << std::setprecision(9) << prefix
      << "relu_backward_op_method=ElementWiseGreater+ElementWiseSelect\n"
      << "tensor_shape=2x5\nmask_datatype=BOOL_8\noutput_datatype=FLOAT_32\n"
      << "zero_boundary_rule=Z1_GT_0\nmax_abs_error=" << ma
      << "\nmean_abs_error=" << me << "\nmax_relative_error=" << mr
      << "\nmask_matches_cpu=" << (maskMatches ? "true" : "false")
      << "\ngraph_create_result=0\ngraph_finalize_result=0\n"
      << "graph_execute_result=0\ncpu_fallback=false\nnan_inf="
      << (!finite(got) ? "true" : "false") << "\nstatus="
      << (ok ? "SUCCESS" : "FAILED") << '\n'
      << rt.diagnostics();
  return out.str();
}
struct Summary {
  double min = 0, median = 0, mean = 0, sd = 0, p90 = 0, p95 = 0, max = 0;
};
Summary summarize(std::vector<double> v) {
  Summary r;
  if (v.empty())
    return r;
  std::sort(v.begin(), v.end());
  r.min = v.front();
  r.max = v.back();
  r.median = v[v.size() / 2];
  r.mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
  double q = 0;
  for (double x : v)
    q += (x - r.mean) * (x - r.mean);
  r.sd = std::sqrt(q / v.size());
  auto pct = [&](double p) {
    return v[std::min(v.size() - 1, size_t(std::ceil(p * v.size()) - 1))];
  };
  r.p90 = pct(.90);
  r.p95 = pct(.95);
  return r;
}
void emitStats(std::ostringstream &s, const char *n,
               const std::vector<double> &v) {
  auto r = summarize(v);
  s << '\n'
    << n << "_min_us=" << r.min << '\n'
    << n << "_median_us=" << r.median << '\n'
    << n << "_mean_us=" << r.mean << '\n'
    << n << "_stddev_us=" << r.sd << '\n'
    << n << "_p90_us=" << r.p90 << '\n'
    << n << "_p95_us=" << r.p95 << '\n'
    << n << "_max_us=" << r.max;
}
std::string train(ExecutionMode mode, const TrainingConfig &c,
                  std::atomic_bool &stop) {
  const bool cpuOnly = mode == ExecutionMode::QNN_CPU_MLP_TRAINING;
  const bool fused = mode == ExecutionMode::QNN_HTP_MLP_FUSED_BACKWARD ||
                     mode ==
                         ExecutionMode::QNN_HTP_MLP_FUSED_BACKWARD_BENCHMARK;
  const bool htpBw = fused ||
                     mode == ExecutionMode::QNN_HTP_MLP_HTP_LINEAR_BACKWARD ||
                     mode == ExecutionMode::QNN_HTP_MLP_BENCHMARK;
  const bool fusedDiagnostic = fused && !c.benchmarkMode;
  auto d = makeData(c);
  const Matrix initialW1 = d.w1, initialW2 = d.w2;
  const float initial = pass(d.x, d.y, d.w1, d.w2).loss;
  Runtime rt;
  std::string e;
  const auto initializationStart = Clock::now();
  if (!cpuOnly &&
      (!rt.initialize(QnnBackendKind::HTP, e) ||
       !rt.prepareMlp(c.batchSize, c.dimension, c.hiddenDimension,
                      c.outputDimension, e, !fused) ||
       (fused && !rt.prepareMlpFusedBackward(fusedDiagnostic, e)) ||
       !rt.setMlpWeights(d.w1.values, d.w2.values, e)))
    return "MLP_TRAINING\nexecution_mode=" +
           std::string(executionModeName(mode)) +
           "\nstatus=FAILED\nfailed_api=HTP_prepare\nerror=" + e +
           "\ncpu_fallback=false";
  const double initializationUs =
      cpuOnly ? 0.0
              : std::chrono::duration<double, std::micro>(Clock::now() -
                                                          initializationStart)
                    .count();
  const int steps =
      c.epochs > 0 ? (c.sampleCount / c.batchSize) * c.epochs : c.steps;
  bool nan = false, dw1Nonzero = false, dw2Nonzero = false,
       dhNonzero = false, dz1Nonzero = false;
  double dw1err = 0, dw2err = 0, dherr = 0, dz1err = 0, dw1mean = 0,
         dw2mean = 0, dhmean = 0, dz1mean = 0, forwardErr = 0,
         updatedForwardErr = 0;
  Matrix lastX, lastY;
  std::vector<double> fullT, forwardT, lossDpT, backward2T, reluT, dw1T,
      optimizerT, updateT;
  for (int st = 0; st < steps && !stop.load(); ++st) {
    auto fullStart = Clock::now();
    Matrix x = batch(d.x, (st * c.batchSize) % c.sampleCount, c.batchSize),
           y = batch(d.y, (st * c.batchSize) % c.sampleCount, c.batchSize);
    lastX = x;
    lastY = y;
    Matrix h(c.batchSize, c.hiddenDimension), p(c.batchSize, c.outputDimension),
        dp(c.batchSize, c.outputDimension),
        dw2(c.hiddenDimension, c.outputDimension),
        dh(c.batchSize, c.hiddenDimension), dz1(c.batchSize, c.hiddenDimension),
        dw1(c.dimension, c.hiddenDimension);
    Pass ref;
    bool check = !c.benchmarkMode && (c.correctnessInterval <= 1 || st == 0 ||
                                      st % c.correctnessInterval == 0);
    if (cpuOnly) {
      ref = pass(x, y, d.w1, d.w2);
      h = ref.h;
      p = ref.p;
      dp = ref.dp;
      dw2 = ref.dw2;
      dh = ref.dh;
      dz1 = ref.dz1;
      dw1 = ref.dw1;
    } else {
      auto t = Clock::now();
      std::vector<float> hv, pv;
      if (!rt.executeMlpForward(x.values, hv, pv, e))
        return "MLP_TRAINING\nexecution_mode=" +
               std::string(executionModeName(mode)) +
               "\nstatus=FAILED\nfailed_api=forward_execute\nerror=" + e +
               "\ncpu_fallback=false";
      forwardT.push_back(
          std::chrono::duration<double, std::micro>(Clock::now() - t).count());
      h = Matrix(c.batchSize, c.hiddenDimension, hv);
      p = Matrix(c.batchSize, c.outputDimension, pv);
      t = Clock::now();
      float sc = 2.0f / (c.batchSize * c.outputDimension);
      for (size_t i = 0; i < dp.values.size(); ++i)
        dp.values[i] = (p.values[i] - y.values[i]) * sc;
      volatile float loss = cpu::meanSquaredError(p, y);
      (void)loss;
      lossDpT.push_back(
          std::chrono::duration<double, std::micro>(Clock::now() - t).count());
      if (check) {
        ref = pass(x, y, d.w1, d.w2);
        forwardErr =
            std::max(forwardErr, std::max(maxAbs(ref.h, h), maxAbs(ref.p, p)));
      }
      if (htpBw) {
        t = Clock::now();
        std::vector<float> w2v, dhv, dzv, w1v;
        std::vector<std::uint8_t> mask;
        if (fused) {
          if (!rt.executeMlpFusedBackward(x.values, h.values, dp.values, w2v,
                                          dhv, mask, dzv, w1v, e))
            return "MLP_TRAINING\nexecution_mode=" +
                   std::string(executionModeName(mode)) +
                   "\nstatus=FAILED\nfailed_api=fused_backward_execute\nerror=" +
                   e + "\ncpu_fallback=false\nhtp_relu_backward_used=false\n"
                       "htp_fused_backward_used=false";
          backward2T.push_back(
              std::chrono::duration<double, std::micro>(Clock::now() - t)
                  .count());
          dw2 = Matrix(c.hiddenDimension, c.outputDimension, w2v);
          dw1 = Matrix(c.dimension, c.hiddenDimension, w1v);
          if (fusedDiagnostic) {
            dh = Matrix(c.batchSize, c.hiddenDimension, dhv);
            dz1 = Matrix(c.batchSize, c.hiddenDimension, dzv);
          }
        } else {
          if (!rt.executeMlpSecondBackward(h.values, dp.values, w2v, dhv, e))
            return "MLP_TRAINING\nexecution_mode=" +
                   std::string(executionModeName(mode)) +
                   "\nstatus=FAILED\nfailed_api=backward2_execute\nerror=" + e +
                   "\ncpu_fallback=false";
          backward2T.push_back(
              std::chrono::duration<double, std::micro>(Clock::now() - t)
                  .count());
          dw2 = Matrix(c.hiddenDimension, c.outputDimension, w2v);
          dh = Matrix(c.batchSize, c.hiddenDimension, dhv);
          t = Clock::now();
          for (size_t i = 0; i < dz1.values.size(); ++i)
            dz1.values[i] = h.values[i] > 0 ? dh.values[i] : 0;
          reluT.push_back(
              std::chrono::duration<double, std::micro>(Clock::now() - t)
                  .count());
          t = Clock::now();
          if (!rt.executeMlpFirstBackward(x.values, dz1.values, w1v, e))
            return "MLP_TRAINING\nexecution_mode=" +
                   std::string(executionModeName(mode)) +
                   "\nstatus=FAILED\nfailed_api=backward1_execute\nerror=" + e +
                   "\ncpu_fallback=false";
          dw1T.push_back(
              std::chrono::duration<double, std::micro>(Clock::now() - t)
                  .count());
          dw1 = Matrix(c.dimension, c.hiddenDimension, w1v);
        }
        if (check) {
          dw1err = std::max(dw1err, maxAbs(ref.dw1, dw1));
          dw2err = std::max(dw2err, maxAbs(ref.dw2, dw2));
          dw1mean = std::max(dw1mean, meanAbs(ref.dw1, dw1));
          dw2mean = std::max(dw2mean, meanAbs(ref.dw2, dw2));
          if (!fused || fusedDiagnostic) {
            dherr = std::max(dherr, maxAbs(ref.dh, dh));
            dz1err = std::max(dz1err, maxAbs(ref.dz1, dz1));
            dhmean = std::max(dhmean, meanAbs(ref.dh, dh));
            dz1mean = std::max(dz1mean, meanAbs(ref.dz1, dz1));
          }
        }
      } else {
        auto t2 = Clock::now();
        dw2 = cpu::matMul(cpu::transpose(h), dp);
        dh = cpu::matMul(dp, cpu::transpose(d.w2));
        for (size_t i = 0; i < dz1.values.size(); ++i)
          dz1.values[i] = h.values[i] > 0 ? dh.values[i] : 0;
        dw1 = cpu::matMul(cpu::transpose(x), dz1);
        backward2T.push_back(
            std::chrono::duration<double, std::micro>(Clock::now() - t2)
                .count());
      }
    }
    dw1Nonzero = dw1Nonzero || std::any_of(dw1.values.begin(), dw1.values.end(),
                                           [](float v) { return v != 0.0f; });
    dw2Nonzero = dw2Nonzero || std::any_of(dw2.values.begin(), dw2.values.end(),
                                           [](float v) { return v != 0.0f; });
    dhNonzero = dhNonzero || std::any_of(dh.values.begin(), dh.values.end(),
                                         [](float v) { return v != 0.0f; });
    dz1Nonzero = dz1Nonzero ||
                 std::any_of(dz1.values.begin(), dz1.values.end(),
                             [](float v) { return v != 0.0f; });
    auto t = Clock::now();
    cpu::sgdUpdate(d.w1, dw1, c.learningRate);
    cpu::sgdUpdate(d.w2, dw2, c.learningRate);
    optimizerT.push_back(
        std::chrono::duration<double, std::micro>(Clock::now() - t).count());
    if (!cpuOnly) {
      t = Clock::now();
      if (!rt.setMlpWeights(d.w1.values, d.w2.values, e))
        return "MLP_TRAINING\nexecution_mode=" +
               std::string(executionModeName(mode)) +
               "\nstatus=FAILED\nfailed_api=weight_update\nerror=" + e +
               "\ncpu_fallback=false";
      updateT.push_back(
          std::chrono::duration<double, std::micro>(Clock::now() - t).count());
    }
    nan = nan || !finite(d.w1) || !finite(d.w2);
    fullT.push_back(
        std::chrono::duration<double, std::micro>(Clock::now() - fullStart)
            .count());
    if (nan)
      break;
  }
  Pass finalPass = pass(d.x, d.y, d.w1, d.w2);
  const float final = finalPass.loss;
  double predictionMax = maxAbs(finalPass.p, d.y);
  if (!cpuOnly && !lastX.values.empty()) {
    std::vector<float> h, p;
    if (!rt.executeMlpForward(lastX.values, h, p, e))
      return "MLP_TRAINING\nexecution_mode=" +
             std::string(executionModeName(mode)) +
             "\nstatus=FAILED\nfailed_api=updated_forward_execute\nerror=" + e +
             "\ncpu_fallback=false";
    auto ref = pass(lastX, lastY, d.w1, d.w2);
    updatedForwardErr =
        maxAbs(ref.p, Matrix(c.batchSize, c.outputDimension, p));
  }
  const bool ok =
      !nan && final < initial &&
      (!htpBw || (dw1err < 1e-2 && dw2err < 1e-2 && dherr < 1e-2)) &&
      updatedForwardErr < 1e-3;
  std::ostringstream s;
  s << std::setprecision(9)
    << "MLP_TRAINING\nexecution_mode=" << executionModeName(mode)
    << "\nseed=" << c.seed << "\nsample_count=" << c.sampleCount
    << "\nbatch_size=" << c.batchSize << "\ninput_dim=" << c.dimension
    << "\nhidden_dim=" << c.hiddenDimension
    << "\noutput_dim=" << c.outputDimension << "\nepochs=" << c.epochs
    << "\nsteps=" << steps << "\nlearning_rate=" << c.learningRate
    << "\ninitial_loss=" << initial << "\nfinal_loss=" << final
    << "\nruntime_initialization_us=" << initializationUs
    << "\nprediction_mse=" << final
    << "\nprediction_max_abs_error=" << predictionMax
    << "\nforward_max_abs_error=" << forwardErr
    << "\ndw1_max_abs_error=" << dw1err << "\ndw2_max_abs_error=" << dw2err
    << "\ndh_max_abs_error=" << dherr << "\ndz1_max_abs_error=" << dz1err
    << "\ndw1_mean_abs_error=" << dw1mean
    << "\ndw2_mean_abs_error=" << dw2mean << "\ndh_mean_abs_error=" << dhmean
    << "\ndz1_mean_abs_error=" << dz1mean
    << "\nupdated_forward_max_abs_error=" << updatedForwardErr
    << "\nforward_backend=" << (cpuOnly ? "CPU" : "HTP")
    << "\ndw2_backend=" << (htpBw ? "HTP" : "CPU")
    << "\ndh_backend=" << (htpBw ? "HTP" : "CPU")
    << "\nrelu_backward_backend=" << (fused ? "HTP" : "CPU")
    << "\ndw1_backend=" << (htpBw ? "HTP" : "CPU")
    << "\noptimizer_backend=CPU\nforward_graph_create_count="
    << (cpuOnly ? 0 : 1)
    << "\nforward_graph_finalize_count=" << (cpuOnly ? 0 : 1)
    << "\nforward_execute_count=" << (cpuOnly ? 0 : steps + 1)
    << "\ndw2_graph_create_count=" << (!cpuOnly && !fused ? 1 : 0)
    << "\ndh_graph_create_count=" << (!cpuOnly && !fused ? 1 : 0)
    << "\ndw1_graph_create_count=" << (!cpuOnly && !fused ? 1 : 0)
    << "\nfused_backward_graph_create_count=" << (fused ? 1 : 0)
    << "\nbackward_graph_finalize_count=" << (cpuOnly ? 0 : (fused ? 1 : 3))
    << "\ndw2_execute_count=" << (htpBw && !fused ? steps : 0)
    << "\ndh_execute_count=" << (htpBw && !fused ? steps : 0)
    << "\ndw1_execute_count=" << (htpBw && !fused ? steps : 0)
    << "\nfused_backward_execute_count=" << (fused ? steps : 0)
    << "\nbackward_execute_count_per_step=" << (fused ? 1 : (htpBw ? 3 : 0))
    << "\nw1_initial_bind_count=" << (cpuOnly ? 0 : 1)
    << "\nw2_initial_bind_count=" << (cpuOnly ? 0 : 1)
    << "\nw1_update_count=" << (cpuOnly ? 0 : steps)
    << "\nw2_update_count=" << (cpuOnly ? 0 : steps)
    << "\nw2_copies_per_step=" << (cpuOnly ? 0 : 1)
    << "\nw1_changed=" << (maxAbs(initialW1, d.w1) > 0 ? "true" : "false")
    << "\nw2_changed=" << (maxAbs(initialW2, d.w2) > 0 ? "true" : "false")
    << "\ndw2_nonzero=" << (dw2Nonzero ? "true" : "false")
    << "\ndh_nonzero=" << (dhNonzero ? "true" : "false")
    << "\ndw1_nonzero=" << (dw1Nonzero ? "true" : "false")
    << "\ndz1_nonzero=" << (dz1Nonzero ? "true" : "false")
    << "\nshared_w2_app_buffer=true\ncross_graph_w2_sync=true";
  emitStats(s, "forward", forwardT);
  emitStats(s, "loss_dp", lossDpT);
  emitStats(s, "second_layer_backward", backward2T);
  emitStats(s, "relu_backward", reluT);
  emitStats(s, "first_layer_dw", dw1T);
  emitStats(s, "optimizer", optimizerT);
  emitStats(s, "weight_update", updateT);
  emitStats(s, "full_step", fullT);
  s << "\ncpu_fallback=false\nhtp_linear_backward_used="
    << (htpBw ? "true" : "false")
    << "\nhtp_relu_backward_used=" << (fused ? "true" : "false")
    << "\nhtp_fused_backward_used=" << (fused ? "true" : "false")
    << "\nnan_detected=" << (nan ? "true" : "false")
    << "\ninf_detected=false\nnan_inf=" << (nan ? "true" : "false")
    << "\nstatus=" << (ok ? "SUCCESS" : "FAILED");
  if (!cpuOnly)
    s << '\n' << rt.diagnostics();
  return s.str();
}
} // namespace
std::string runMlpExperiment(ExecutionMode mode, const TrainingConfig &c,
                             std::atomic_bool &stop, const LogSink &log) {
  std::string r;
  if (mode == ExecutionMode::QNN_MLP_GRADIENT_CHECK)
    r = gradientCheck();
  else if (mode == ExecutionMode::QNN_HTP_DX_CHECK)
    r = dxCheck(c);
  else if (mode == ExecutionMode::QNN_HTP_RELU_BACKWARD_CHECK)
    r = reluBackwardCheck();
  else
    r = train(mode, c, stop);
  if (log)
    log(r);
  return r;
}
} // namespace phonelm::qnn