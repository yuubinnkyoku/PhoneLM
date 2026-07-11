#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace phonelm::qnn::host {

// SDK-independent affine quantization math. These fields are deliberately not
// QNN enums or QNN quantization structs. A version-specific QAIRT adapter must
// map them only after the installed SDK contract has been verified.
struct AffineQuantizationParameters {
    float scale = 0.0f;
    std::int32_t zeroPoint = 0;
    std::int32_t minimumCode = 0;
    std::int32_t maximumCode = 0;

    bool valid() const;
};

struct QuantizationResult {
    std::vector<std::int32_t> values;
    double saturatedValueRatio = 0.0;
};

AffineQuantizationParameters chooseSignedSymmetricParameters(
    const std::vector<float>& values,
    int bitWidth);

AffineQuantizationParameters chooseSignedAffineParameters(
    const std::vector<float>& values,
    int bitWidth);

QuantizationResult quantize(const std::vector<float>& values,
                            const AffineQuantizationParameters& parameters);

std::vector<float> dequantize(const std::vector<std::int32_t>& values,
                              const AffineQuantizationParameters& parameters);

double zeroGradientRatio(const std::vector<float>& referenceValues,
                         const std::vector<std::int32_t>& quantizedValues,
                         const AffineQuantizationParameters& parameters,
                         float nonzeroEpsilon = 0.0f);

std::optional<std::size_t> checkedElementCount(const std::vector<std::size_t>& shape);

std::optional<std::size_t> checkedBufferSize(const std::vector<std::size_t>& shape,
                                             std::size_t elementSize);

}  // namespace phonelm::qnn::host
