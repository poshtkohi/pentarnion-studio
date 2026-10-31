#pragma once
#include "language/frontend.hpp"
#include "algebra/universal.hpp"
#include <atomic>
#include <cstddef>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>
namespace penta::runtime::detail {
struct TensorError:std::runtime_error{int status;TensorError(int code,const char* message):std::runtime_error(message),status(code){}};
struct TensorLimits{std::size_t elements=1000000,coordinates=8000000,bytes=1073741824;};
// Entry order is column-major across all dimensions. Each element's complete
// algebra coefficients are adjacent. This does not alter 2D matrix storage.
struct TensorData{
    language::Type entryType=language::Type::Real;
    algebra::universal::DefinitionPtr algebra;
    double gamma=1;
    std::vector<std::size_t> shape;
    std::vector<double> values;
    [[nodiscard]] std::size_t coefficientWidth()const;
    [[nodiscard]] std::size_t elements()const;
    [[nodiscard]] std::size_t linearIndex(std::span<const std::size_t> zeroBasedIndices)const;
    [[nodiscard]] std::size_t retainedBytes()const noexcept;
};
using TensorValue=std::shared_ptr<const TensorData>;
[[nodiscard]] bool sameTensorDescriptor(const TensorData&,const TensorData&)noexcept;
[[nodiscard]] std::size_t tensorElements(std::span<const std::size_t>,std::size_t maximum=1000000);
[[nodiscard]] TensorValue makeTensor(language::Type,algebra::universal::DefinitionPtr,double gamma,
    std::vector<std::size_t> shape,std::vector<double> values,const TensorLimits& = {},
    const std::atomic_bool* cancellation=nullptr);
}
