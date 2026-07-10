#include "qnn_tensor.h"

#include <sstream>

namespace phonelm::qnn {

std::string shapeString(const TensorDescriptor& tensor) {
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < tensor.dimensions.size(); ++index) {
        if (index > 0) stream << ',';
        stream << tensor.dimensions[index];
    }
    stream << ']';
    return stream.str();
}

}  // namespace phonelm::qnn

