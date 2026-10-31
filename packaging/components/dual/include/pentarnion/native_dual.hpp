#pragma once
// Declaration-only, pinned-GCC first-order channels. Implementations are compiled.
#include "pentarnion/native_base.hpp"
namespace penta_generated {
template<std::size_t N> struct Dual {
    long double v=0;
    std::array<long double,N> d{};
    bool stateDependent=false;
    PENTA_NATIVE_API Dual();
    PENTA_NATIVE_API Dual(long double x);
};
template<std::size_t N> PENTA_NATIVE_API long double value(const Dual<N>& x);
template<std::size_t N> PENTA_NATIVE_API Dual<N> operator+(const Dual<N>& a,const Dual<N>& b);
template<std::size_t N> PENTA_NATIVE_API Dual<N> operator-(const Dual<N>& a,const Dual<N>& b);
template<std::size_t N> PENTA_NATIVE_API Dual<N> operator-(const Dual<N>& a);
template<std::size_t N> PENTA_NATIVE_API Dual<N> operator*(const Dual<N>& a,const Dual<N>& b);
template<std::size_t N> PENTA_NATIVE_API Dual<N> operator/(const Dual<N>& a,const Dual<N>& b);
template<std::size_t N> PENTA_NATIVE_API Dual<N> divide(const Dual<N>& a,const Dual<N>& b);
template<std::size_t N> PENTA_NATIVE_API Dual<N> math_sin(const Dual<N>& x);
template<std::size_t N> PENTA_NATIVE_API Dual<N> math_cos(const Dual<N>& x);
template<std::size_t N> PENTA_NATIVE_API Dual<N> math_tan(const Dual<N>& x);
template<std::size_t N> PENTA_NATIVE_API Dual<N> math_exp(const Dual<N>& x);
template<std::size_t N> PENTA_NATIVE_API Dual<N> math_log(const Dual<N>& x);
template<std::size_t N> PENTA_NATIVE_API Dual<N> math_sqrt(const Dual<N>& x);
template<std::size_t N> PENTA_NATIVE_API Dual<N> math_sinh(const Dual<N>& x);
template<std::size_t N> PENTA_NATIVE_API Dual<N> math_cosh(const Dual<N>& x);
template<std::size_t N> PENTA_NATIVE_API Dual<N> math_tanh(const Dual<N>& x);
} // namespace penta_generated
