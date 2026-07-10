#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace phonelm::qnn {

// SDK-independent description used by the training engine. It intentionally
// contains no guessed QNN enum or struct. A verified SDK adapter must translate
// this only after reading the installed SDK's headers and official samples.
struct TensorDescriptor {
    std::string name;
    std::vector<std::size_t> dimensions;
    std::string dataType = "UNVERIFIED";
    std::string quantizationType = "UNVERIFIED";
    float quantScale = 0.0f;
    int quantOffset = 0;
};

std::string shapeString(const TensorDescriptor& tensor);

}  // namespace phonelm::qnn

