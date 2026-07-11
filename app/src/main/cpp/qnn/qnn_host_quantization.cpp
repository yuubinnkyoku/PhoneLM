#include "qnn_host_quantization.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace phonelm::qnn::host {
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::invalid_argument(message);
}

std::int32_t signedMaximumCode(int bitWidth) {
    require(bitWidth >= 2 && bitWidth <= 16, "bit width must be in [2, 16]");
    return static_cast<std::int32_t>((std::uint32_t{1} << (bitWidth - 1)) - 1U);
}

void requireFinite(const std::vector<float>& values) {
    require(!values.empty(), "quantization input must not be empty");
    for (const float value : values) {
        require(std::isfinite(value), "quantization input must be finite");
    }
}

}  // namespace

bool AffineQuantizationParameters::valid() const {
    return std::isfinite(scale) && scale > 0.0f && minimumCode < maximumCode &&
           zeroPoint >= minimumCode && zeroPoint <= maximumCode;
}

AffineQuantizationParameters chooseSignedSymmetricParameters(
    const std::vector<float>& values,
    int bitWidth) {
    requireFinite(values);
    const std::int32_t maximumCode = signedMaximumCode(bitWidth);
    float maximumMagnitude = 0.0f;
    for (const float value : values) {
        maximumMagnitude = std::max(maximumMagnitude, std::fabs(value));
    }
    return {
        maximumMagnitude == 0.0f ? 1.0f : maximumMagnitude / maximumCode,
        0,
        -maximumCode,
        maximumCode,
    };
}

AffineQuantizationParameters chooseSignedAffineParameters(
    const std::vector<float>& values,
    int bitWidth) {
    requireFinite(values);
    const std::int32_t maximumCode = signedMaximumCode(bitWidth);
    const std::int32_t minimumCode = -maximumCode - 1;
    const auto [minimumIterator, maximumIterator] =
        std::minmax_element(values.begin(), values.end());
    const float minimumValue = std::min(0.0f, *minimumIterator);
    const float maximumValue = std::max(0.0f, *maximumIterator);
    if (minimumValue == maximumValue) {
        return {1.0f, 0, minimumCode, maximumCode};
    }

    const float scale = (maximumValue - minimumValue) /
                        static_cast<float>(maximumCode - minimumCode);
    const auto unclampedZeroPoint = static_cast<long>(
        std::lround(static_cast<double>(minimumCode) - minimumValue / scale));
    const auto zeroPoint = static_cast<std::int32_t>(std::clamp<long>(
        unclampedZeroPoint, minimumCode, maximumCode));
    return {scale, zeroPoint, minimumCode, maximumCode};
}

QuantizationResult quantize(const std::vector<float>& values,
                            const AffineQuantizationParameters& parameters) {
    require(parameters.valid(), "invalid quantization parameters");
    requireFinite(values);
    QuantizationResult result;
    result.values.reserve(values.size());
    std::size_t saturatedValues = 0;
    for (const float value : values) {
        const double transformed = static_cast<double>(value) / parameters.scale +
                                   parameters.zeroPoint;
        std::int32_t code = 0;
        if (transformed <= parameters.minimumCode) {
            code = parameters.minimumCode;
        } else if (transformed >= parameters.maximumCode) {
            code = parameters.maximumCode;
        } else {
            code = static_cast<std::int32_t>(std::llround(transformed));
        }
        if (code == parameters.minimumCode || code == parameters.maximumCode) {
            ++saturatedValues;
        }
        result.values.push_back(code);
    }
    result.saturatedValueRatio = static_cast<double>(saturatedValues) /
                                 static_cast<double>(values.size());
    return result;
}

std::vector<float> dequantize(const std::vector<std::int32_t>& values,
                              const AffineQuantizationParameters& parameters) {
    require(parameters.valid(), "invalid quantization parameters");
    std::vector<float> result;
    result.reserve(values.size());
    for (const std::int32_t value : values) {
        require(value >= parameters.minimumCode && value <= parameters.maximumCode,
                "quantized value is outside the declared code range");
        result.push_back((value - parameters.zeroPoint) * parameters.scale);
    }
    return result;
}

double zeroGradientRatio(const std::vector<float>& referenceValues,
                         const std::vector<std::int32_t>& quantizedValues,
                         const AffineQuantizationParameters& parameters,
                         float nonzeroEpsilon) {
    require(parameters.valid(), "invalid quantization parameters");
    require(referenceValues.size() == quantizedValues.size(),
            "reference and quantized gradient sizes do not match");
    require(std::isfinite(nonzeroEpsilon) && nonzeroEpsilon >= 0.0f,
            "nonzero epsilon must be finite and non-negative");
    std::size_t nonzeroReferenceValues = 0;
    std::size_t roundedToZeroValues = 0;
    for (std::size_t index = 0; index < referenceValues.size(); ++index) {
        require(std::isfinite(referenceValues[index]), "gradient must be finite");
        if (std::fabs(referenceValues[index]) > nonzeroEpsilon) {
            ++nonzeroReferenceValues;
            if (quantizedValues[index] == parameters.zeroPoint) {
                ++roundedToZeroValues;
            }
        }
    }
    if (nonzeroReferenceValues == 0) return 0.0;
    return static_cast<double>(roundedToZeroValues) /
           static_cast<double>(nonzeroReferenceValues);
}

std::optional<std::size_t> checkedElementCount(const std::vector<std::size_t>& shape) {
    if (shape.empty()) return std::nullopt;
    std::size_t result = 1;
    for (const std::size_t dimension : shape) {
        if (dimension == 0 || result > std::numeric_limits<std::size_t>::max() / dimension) {
            return std::nullopt;
        }
        result *= dimension;
    }
    return result;
}

std::optional<std::size_t> checkedBufferSize(const std::vector<std::size_t>& shape,
                                             std::size_t elementSize) {
    if (elementSize == 0) return std::nullopt;
    const auto elementCount = checkedElementCount(shape);
    if (!elementCount ||
        *elementCount > std::numeric_limits<std::size_t>::max() / elementSize) {
        return std::nullopt;
    }
    return *elementCount * elementSize;
}

}  // namespace phonelm::qnn::host
