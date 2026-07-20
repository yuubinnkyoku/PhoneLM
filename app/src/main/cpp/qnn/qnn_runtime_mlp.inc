template <class I, class S>
static bool prepareSlot(I *i, RuntimeMetrics &metrics, S &s,
                        const char *graphName, uint32_t a0, uint32_t a1,
                        uint32_t b0, uint32_t b1, uint32_t o0, uint32_t o1,
                        bool trans0, bool trans1, std::string &error) {
  s.d0[0] = a0;
  s.d0[1] = a1;
  s.d1[0] = b0;
  s.d1[1] = b1;
  s.doo[0] = o0;
  s.doo[1] = o1;
  s.n0 = a0 * a1;
  s.n1 = b0 * b1;
  s.no = o0 * o1;
  tensor(s.in[0], "a", QNN_TENSOR_TYPE_APP_WRITE, s.d0);
  tensor(s.in[1], "b", QNN_TENSOR_TYPE_APP_WRITE, s.d1);
  tensor(s.out, "out", QNN_TENSOR_TYPE_APP_READ, s.doo);
  auto st = i->api.graphCreate(i->context, graphName, nullptr, &s.graph);
  ++metrics.graphCreateCount;
  if (st != QNN_SUCCESS) {
    error = std::string(graphName) +
            " graphCreate=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  for (auto &t : s.in) {
    st = i->api.tensorCreateGraphTensor(s.graph, &t);
    if (st != QNN_SUCCESS) {
      error = std::string(graphName) +
              " input tensor=" + std::to_string(QNN_GET_ERROR_CODE(st));
      return false;
    }
  }
  st = i->api.tensorCreateGraphTensor(s.graph, &s.out);
  if (st != QNN_SUCCESS) {
    error = std::string(graphName) +
            " output tensor=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  Qnn_Param_t ps[2] = {QNN_PARAM_INIT, QNN_PARAM_INIT};
  Qnn_Scalar_t sc[2] = {QNN_SCALAR_INIT, QNN_SCALAR_INIT};
  int pc = 0;
  if (trans0) {
    sc[pc].dataType = QNN_DATATYPE_BOOL_8;
    sc[pc].bool8Value = true;
    ps[pc].paramType = QNN_PARAMTYPE_SCALAR;
    ps[pc].name = QNN_OP_MAT_MUL_PARAM_TRANSPOSE_IN0;
    ps[pc].scalarParam = sc[pc];
    ++pc;
  }
  if (trans1) {
    sc[pc].dataType = QNN_DATATYPE_BOOL_8;
    sc[pc].bool8Value = true;
    ps[pc].paramType = QNN_PARAMTYPE_SCALAR;
    ps[pc].name = QNN_OP_MAT_MUL_PARAM_TRANSPOSE_IN1;
    ps[pc].scalarParam = sc[pc];
    ++pc;
  }
  Qnn_OpConfig_t op = QNN_OPCONFIG_INIT;
  op.v1.name = "matmul";
  op.v1.packageName = QNN_OP_PACKAGE_NAME_QTI_AISW;
  op.v1.typeName = QNN_OP_MAT_MUL;
  op.v1.numOfParams = pc;
  op.v1.params = pc ? ps : nullptr;
  op.v1.numOfInputs = 2;
  op.v1.inputTensors = s.in;
  op.v1.numOfOutputs = 1;
  op.v1.outputTensors = &s.out;
  st = i->api.graphAddNode(s.graph, op);
  if (st != QNN_SUCCESS) {
    error = std::string(graphName) +
            " graphAddNode=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  st = i->api.graphFinalize(s.graph, nullptr, nullptr);
  ++metrics.graphFinalizeCount;
  if (st != QNN_SUCCESS) {
    error = std::string(graphName) +
            " graphFinalize=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  return true;
}
template <class I, class S>
static bool executeSlot(I *i, RuntimeMetrics &metrics, S &s,
                        const std::vector<float> &a,
                        const std::vector<float> &b, std::vector<float> &o,
                        std::string &error) {
  if (!s.graph || a.size() != s.n0 || b.size() != s.n1) {
    error = "slot buffer size mismatch";
    return false;
  }
  o.resize(s.no);
  s.in[0].v1.clientBuf = {const_cast<float *>(a.data()),
                          uint32_t(a.size() * sizeof(float))};
  s.in[1].v1.clientBuf = {const_cast<float *>(b.data()),
                          uint32_t(b.size() * sizeof(float))};
  s.out.v1.clientBuf = {o.data(), uint32_t(o.size() * sizeof(float))};
  auto st = i->api.graphExecute(s.graph, s.in, 2, &s.out, 1, nullptr, nullptr);
  ++metrics.graphExecuteCount;
  if (st != QNN_SUCCESS) {
    error = "graphExecute=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  return true;
}

bool Runtime::prepareInputGradientMatMul(uint32_t b, uint32_t in, uint32_t out,
                                         std::string &e) {
  bool ok = prepareSlot(impl_, metrics_, impl_->dx, "phonelm_dx", b, out, in,
                        out, b, in, false, true, e);
  if (ok)
    diagnostics_ += "dx_graph_create=success\ndx_graph_finalize=success\ndx_"
                    "transpose_implementation=QNN_MATMUL_TRANSPOSE_IN1\n";
  return ok;
}
bool Runtime::executeInputGradient(const std::vector<float> &dp,
                                   const std::vector<float> &w,
                                   std::vector<float> &dx, std::string &e) {
  return executeSlot(impl_, metrics_, impl_->dx, dp, w, dx, e);
}

bool Runtime::prepareMlp(uint32_t b, uint32_t in, uint32_t h, uint32_t out,
                         std::string &e) {
  if (!impl_ || !impl_->context) {
    e = "runtime not initialized";
    return false;
  }
  auto *i = impl_;
  i->batch = b;
  i->inDim = in;
  i->hidDim = h;
  i->outDim = out;
  i->xDims[0] = b;
  i->xDims[1] = in;
  i->w1Dims[0] = in;
  i->w1Dims[1] = h;
  i->w2Dims[0] = h;
  i->w2Dims[1] = out;
  i->zDims[0] = b;
  i->zDims[1] = h;
  i->pDims[0] = b;
  i->pDims[1] = out;
  tensor(i->mlpInputs[0], "mlp_x", QNN_TENSOR_TYPE_APP_WRITE, i->xDims);
  tensor(i->mlpInputs[1], "mlp_w1", QNN_TENSOR_TYPE_APP_WRITE, i->w1Dims);
  tensor(i->mlpInputs[2], "mlp_w2", QNN_TENSOR_TYPE_APP_WRITE, i->w2Dims);
  tensor(i->mlpZ, "mlp_z1", QNN_TENSOR_TYPE_NATIVE, i->zDims);
  tensor(i->mlpH, "mlp_h", QNN_TENSOR_TYPE_APP_READ, i->zDims);
  tensor(i->mlpP, "mlp_p", QNN_TENSOR_TYPE_APP_READ, i->pDims);
  auto st = i->api.graphCreate(i->context, "phonelm_mlp_forward", nullptr,
                               &i->mlpForward);
  ++metrics_.graphCreateCount;
  if (st != QNN_SUCCESS) {
    e = "mlp forward graphCreate=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  for (auto &t : i->mlpInputs) {
    st = i->api.tensorCreateGraphTensor(i->mlpForward, &t);
    if (st != QNN_SUCCESS) {
      e = "mlp input tensorCreate=" + std::to_string(QNN_GET_ERROR_CODE(st));
      return false;
    }
  }
  for (auto *t : {&i->mlpZ, &i->mlpH, &i->mlpP}) {
    st = i->api.tensorCreateGraphTensor(i->mlpForward, t);
    if (st != QNN_SUCCESS) {
      e = "mlp output tensorCreate=" + std::to_string(QNN_GET_ERROR_CODE(st));
      return false;
    }
  }
  auto add = [&](const char *n, const char *type, Qnn_Tensor_t *ins,
                 uint32_t ni, Qnn_Tensor_t *outs, uint32_t no) {
    Qnn_OpConfig_t op = QNN_OPCONFIG_INIT;
    op.v1.name = n;
    op.v1.packageName = QNN_OP_PACKAGE_NAME_QTI_AISW;
    op.v1.typeName = type;
    op.v1.numOfInputs = ni;
    op.v1.inputTensors = ins;
    op.v1.numOfOutputs = no;
    op.v1.outputTensors = outs;
    return i->api.graphAddNode(i->mlpForward, op);
  };
  Qnn_Tensor_t mm1in[2] = {i->mlpInputs[0], i->mlpInputs[1]};
  if ((st = add("mlp_mm1", QNN_OP_MAT_MUL, mm1in, 2, &i->mlpZ, 1)) !=
      QNN_SUCCESS) {
    e = "mlp MatMul1=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  if ((st = add("mlp_relu", QNN_OP_RELU, &i->mlpZ, 1, &i->mlpH, 1)) !=
      QNN_SUCCESS) {
    e = "mlp Relu=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  Qnn_Tensor_t mm2in[2] = {i->mlpH, i->mlpInputs[2]};
  if ((st = add("mlp_mm2", QNN_OP_MAT_MUL, mm2in, 2, &i->mlpP, 1)) !=
      QNN_SUCCESS) {
    e = "mlp MatMul2=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  st = i->api.graphFinalize(i->mlpForward, nullptr, nullptr);
  ++metrics_.graphFinalizeCount;
  if (st != QNN_SUCCESS) {
    e = "mlp forward graphFinalize=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  if (!prepareSlot(i, metrics_, i->dw2, "phonelm_mlp_dw2", b, h, b, out, h, out,
                   true, false, e))
    return false;
  if (!prepareSlot(i, metrics_, i->dh, "phonelm_mlp_dh", b, out, h, out, b, h,
                   false, true, e))
    return false;
  if (!prepareSlot(i, metrics_, i->dw1, "phonelm_mlp_dw1", b, in, b, h, in, h,
                   true, false, e))
    return false;
  diagnostics_ += "mlp_forward_graph_create=success\nmlp_forward_graph_"
                  "finalize=success\nmlp_relu_backend=HTP\nmlp_backward_graphs="
                  "dw2,dh,dw1\nbackend_device_context_shared=true\n";
  return true;
}
bool Runtime::setMlpWeights(const std::vector<float> &w1,
                            const std::vector<float> &w2, std::string &e) {
  auto *i = impl_;
  if (!i || !i->mlpForward || w1.size() != i->inDim * i->hidDim ||
      w2.size() != i->hidDim * i->outDim) {
    e = "MLP weight size mismatch";
    return false;
  }
  i->mlpW1 = w1;
  i->mlpW2 = w2;
  ++metrics_.runtimeWeightUpdateCount;
  return true;
}
bool Runtime::executeMlpForward(const std::vector<float> &x,
                                std::vector<float> &h, std::vector<float> &p,
                                std::string &e) {
  auto *i = impl_;
  if (!i || !i->mlpForward || x.size() != i->batch * i->inDim) {
    e = "MLP forward input mismatch";
    return false;
  }
  h.resize(i->batch * i->hidDim);
  p.resize(i->batch * i->outDim);
  i->mlpInputs[0].v1.clientBuf = {const_cast<float *>(x.data()),
                                  uint32_t(x.size() * 4)};
  i->mlpInputs[1].v1.clientBuf = {i->mlpW1.data(),
                                  uint32_t(i->mlpW1.size() * 4)};
  i->mlpInputs[2].v1.clientBuf = {i->mlpW2.data(),
                                  uint32_t(i->mlpW2.size() * 4)};
  Qnn_Tensor_t outs[2] = {i->mlpH, i->mlpP};
  outs[0].v1.clientBuf = {h.data(), uint32_t(h.size() * 4)};
  outs[1].v1.clientBuf = {p.data(), uint32_t(p.size() * 4)};
  auto st = i->api.graphExecute(i->mlpForward, i->mlpInputs, 3, outs, 2,
                                nullptr, nullptr);
  ++metrics_.graphExecuteCount;
  if (st != QNN_SUCCESS) {
    e = "mlp forward graphExecute=" + std::to_string(QNN_GET_ERROR_CODE(st));
    return false;
  }
  return true;
}
bool Runtime::executeMlpSecondBackward(const std::vector<float> &h,
                                       const std::vector<float> &dp,
                                       std::vector<float> &dw2,
                                       std::vector<float> &dh, std::string &e) {
  return executeSlot(impl_, metrics_, impl_->dw2, h, dp, dw2, e) &&
         executeSlot(impl_, metrics_, impl_->dh, dp, impl_->mlpW2, dh, e);
}
bool Runtime::executeMlpFirstBackward(const std::vector<float> &x,
                                      const std::vector<float> &dz,
                                      std::vector<float> &dw1, std::string &e) {
  return executeSlot(impl_, metrics_, impl_->dw1, x, dz, dw1, e);
}