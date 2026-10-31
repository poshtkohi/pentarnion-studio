#pragma once
// Declaration-only, toolchain-pinned model interface. No numerical algorithms.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#if defined(_WIN32)
# if defined(PENTA_NATIVE_BRIDGE_BUILD)
#  define PENTA_NATIVE_API __declspec(dllexport)
# else
#  define PENTA_NATIVE_API __declspec(dllimport)
# endif
#else
# define PENTA_NATIVE_API __attribute__((visibility("default")))
#endif

extern "C" {
PENTA_NATIVE_API unsigned penta_native_bridge_abi_v1();
PENTA_NATIVE_API std::size_t penta_native_bridge_long_double_size_v1();
}

namespace penta_generated {
struct Jet2 {
    long double v=0,x=0,y=0,xy=0;
    bool dependent=false;
    PENTA_NATIVE_API Jet2();
    PENTA_NATIVE_API Jet2(long double);
    PENTA_NATIVE_API explicit operator bool() const;
};
struct Jet3 {
    std::array<long double,8> c{};
    bool dependent=false;
    PENTA_NATIVE_API Jet3();
    PENTA_NATIVE_API Jet3(long double);
    PENTA_NATIVE_API explicit operator bool() const;
};
template<class S> struct Pent { std::array<S,5> c{}; };

PENTA_NATIVE_API long double checked(long double x);
PENTA_NATIVE_API long double value(long double x);
PENTA_NATIVE_API long double divide(long double a,long double b);
template<class S> PENTA_NATIVE_API S integerPower(S a,unsigned exponent);
template<class S> PENTA_NATIVE_API Pent<S> pent(S a,S b,S c,S d,S e);
template<class S> PENTA_NATIVE_API Pent<S> basis(unsigned i);
template<class S> PENTA_NATIVE_API Pent<S> lift(S x);
template<class S> PENTA_NATIVE_API Pent<S> operator+(const Pent<S>&a,const Pent<S>&b);
template<class S> PENTA_NATIVE_API Pent<S> operator-(const Pent<S>&a,const Pent<S>&b);
template<class S> PENTA_NATIVE_API Pent<S> operator-(const Pent<S>&a);
template<class S> PENTA_NATIVE_API Pent<S> operator*(const Pent<S>&a,const S&b);
template<class S> PENTA_NATIVE_API Pent<S> operator*(const S&a,const Pent<S>&b);
template<class S> PENTA_NATIVE_API Pent<S> divide(const Pent<S>&a,const S&b);
template<class S> PENTA_NATIVE_API Pent<S> product(const Pent<S>&a,const Pent<S>&b,double gamma);
template<class S> PENTA_NATIVE_API Pent<S> power(Pent<S>a,unsigned exponent,double gamma);
template<class S> PENTA_NATIVE_API Pent<S> conjugate(Pent<S>a);
template<class S> PENTA_NATIVE_API Pent<S> pure(Pent<S>a);
template<class S> PENTA_NATIVE_API S norm2(const Pent<S>&a);
template<class S> PENTA_NATIVE_API Pent<S> commutator(const Pent<S>&a,const Pent<S>&b,double g);
template<class S> PENTA_NATIVE_API Pent<S> associator(const Pent<S>&a,const Pent<S>&b,const Pent<S>&c,double g);
PENTA_NATIVE_API long double math_sin(long double x);
PENTA_NATIVE_API long double math_cos(long double x);
PENTA_NATIVE_API long double math_tan(long double x);
PENTA_NATIVE_API long double math_exp(long double x);
PENTA_NATIVE_API long double math_log(long double x);
PENTA_NATIVE_API long double math_sqrt(long double x);
PENTA_NATIVE_API long double math_sinh(long double x);
PENTA_NATIVE_API long double math_cosh(long double x);
PENTA_NATIVE_API long double math_tanh(long double x);
PENTA_NATIVE_API double binary64(long double x);
PENTA_NATIVE_API long double value(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_unary(const Jet2&a,long double f,long double d,long double d2);
PENTA_NATIVE_API Jet2 operator+(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API Jet2 operator-(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API Jet2 operator-(const Jet2&a);
PENTA_NATIVE_API Jet2 operator+(const Jet2&a);
PENTA_NATIVE_API Jet2 operator*(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API Jet2 operator/(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API Jet2 divide(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API bool cb_compare(const Jet2&a,const Jet2&b,unsigned op);
PENTA_NATIVE_API bool operator==(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API bool operator!=(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API bool operator<(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API bool operator<=(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API bool operator>(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API bool operator>=(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API bool operator!(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_sin(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_cos(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_exp(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_expm1(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_log(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_log1p(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_log10(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_log2(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_sqrt(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_sinh(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_cosh(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_tanh(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_tan(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_asin(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_acos(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_atan(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_asinh(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_acosh(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_atanh(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_abs(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_floor(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_ceil(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_round(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_sign(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_erf(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_erfc(const Jet2&a);
PENTA_NATIVE_API Jet2 cb_hypot(const Jet2&a,const Jet2&b);
PENTA_NATIVE_API Jet2 cb_atan2(const Jet2&y,const Jet2&x);
PENTA_NATIVE_API Jet2 cb_ipow(Jet2 a,long long n);
template<class S> PENTA_NATIVE_API Pent<S> cb_inverse(const Pent<S>&a);
template<class S> PENTA_NATIVE_API Pent<S> cb_pent_power(Pent<S>a,long long n,double gamma);
PENTA_NATIVE_API Jet2 cb_even(const Jet2&w,bool hyper,bool sinc);
PENTA_NATIVE_API Pent<Jet2> cb_whole(const Pent<Jet2>&p,unsigned op);
template<class S> PENTA_NATIVE_API bool pent_equal(const Pent<S>&a,const Pent<S>&b);
PENTA_NATIVE_API long double value(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_unary(const Jet3& a,long double f,long double d,long double d2,long double d3);
PENTA_NATIVE_API Jet3 operator+(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API Jet3 operator-(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API Jet3 operator-(const Jet3& a);
PENTA_NATIVE_API Jet3 operator+(const Jet3& a);
PENTA_NATIVE_API Jet3 operator*(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API Jet3 operator/(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API Jet3 divide(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API bool cb_compare(const Jet3& a,const Jet3& b,unsigned operation);
PENTA_NATIVE_API bool operator==(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API bool operator!=(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API bool operator<(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API bool operator<=(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API bool operator>(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API bool operator>=(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API bool operator!(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_sin(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_cos(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_exp(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_expm1(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_log(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_log1p(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_log10(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_log2(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_sqrt(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_sinh(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_cosh(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_tanh(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_tan(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_asin(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_acos(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_atan(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_asinh(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_acosh(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_atanh(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_abs(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_floor(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_ceil(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_round(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_sign(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_erf(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_erfc(const Jet3& a);
PENTA_NATIVE_API Jet3 cb_hypot(const Jet3& a,const Jet3& b);
PENTA_NATIVE_API Jet3 cb_atan2(const Jet3& y,const Jet3& x);
PENTA_NATIVE_API Jet3 cb_ipow(Jet3 a,long long exponent);
PENTA_NATIVE_API Jet3 cb_even(const Jet3& w,bool hyper,bool sinc);
PENTA_NATIVE_API Pent<Jet3> cb_whole(const Pent<Jet3>& p,unsigned operation);
} // namespace penta_generated
