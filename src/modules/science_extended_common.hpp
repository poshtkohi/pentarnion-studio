#pragma once
#include "science_extended_api.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>
namespace penta::science::extended {
using Json=nlohmann::json;using Complex=std::complex<double>;
struct Error:std::runtime_error {int status;Error(int s,const std::string& m):std::runtime_error(m),status(s){}};
inline void require(bool yes,const char* text){if(!yes)throw Error(PENTA_NUMERIC_ARGUMENT,text);}
inline void message(char* out,std::uint64_t capacity,const char* text){if(out&&capacity){const auto size=std::min<std::uint64_t>(capacity-1,std::strlen(text));std::memcpy(out,text,size);out[size]=0;}}
inline double finite(double x){if(!std::isfinite(x))throw Error(PENTA_NUMERIC_DOMAIN,"Scientific result is not finite binary64.");return x;}
inline std::size_t integer(const Json& options,const char* key,std::size_t fallback,std::size_t low,std::size_t high){const auto at=options.find(key);if(at==options.end())return fallback;if(at->is_boolean()&&low==0&&high==1)return at->get<bool>()?1:0;require(at->is_number_unsigned()||(at->is_number_integer()&&at->get<std::int64_t>()>=0),"Scientific count options require nonnegative integers.");const auto value=at->get<std::uint64_t>();require(value>=low&&value<=high,"Scientific count is outside its bounded range.");return static_cast<std::size_t>(value);}
inline double number(const Json& options,const char* key,double fallback){const auto at=options.find(key);if(at==options.end())return fallback;require(at->is_number(),"Scientific numeric option must be real.");return finite(at->get<double>());}
inline std::string text(const Json& options,const char* key,const std::string& fallback){const auto at=options.find(key);if(at==options.end())return fallback;require(at->is_string()&&at->get_ref<const std::string&>().size()<=128,"Scientific text option must be bounded.");return at->get<std::string>();}
struct Work {
    const penta_science_host_v2& host;std::uint64_t limit;std::atomic_uint64_t used{0};
    void check()const{if(host.cancelled&&host.cancelled(host.context))throw Error(PENTA_NUMERIC_CANCELLED,"Scientific operation cancelled.");}
    void spend(std::uint64_t count){auto before=used.load();for(;;){if(count>limit-before)throw Error(7,"Scientific native operation budget reached.");if(used.compare_exchange_weak(before,before+count))break;}check();if(host.charge){char error[1024]{};if(host.charge(host.context,count,error,sizeof(error)))throw Error(7,error[0]?error:"Scientific shared work budget reached.");}}
};
void signal(const penta_science_request_v2&,const Json&,Work&,Json&,std::vector<double>&);
void distribution(const penta_science_request_v2&,const Json&,Work&,Json&,std::vector<double>&);
void bootstrap(const penta_science_request_v2&,const Json&,Work&,Json&,std::vector<double>&);
inline void radix(std::vector<Complex>& a,bool inverse,Work& work){const auto n=a.size();for(std::size_t i=1,j=0;i<n;++i){auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}for(std::size_t length=2;length<=n;length<<=1){work.spend(n);const double phase=(inverse?2:-2)*std::acos(-1.)/length;const Complex root(std::cos(phase),std::sin(phase));for(std::size_t first=0;first<n;first+=length){Complex factor=1;for(std::size_t c=0;c<length/2;++c){const auto x=a[first+c],y=a[first+c+length/2]*factor;a[first+c]=x+y;a[first+c+length/2]=x-y;factor*=root;}}}if(inverse)for(auto& x:a)x/=n;}
inline void transform(std::vector<Complex>& a,bool inverse,Work& work){const auto n=a.size();if(n<=1)return;if(inverse){for(auto& x:a)x=std::conj(x);transform(a,false,work);for(auto& x:a)x=std::conj(x)/static_cast<double>(n);return;}if((n&(n-1))==0){radix(a,false,work);return;}std::size_t m=1;while(m<2*n-1)m<<=1;std::vector<Complex> x(m),y(m);const auto pi=std::acos(-1.);for(std::size_t c=0;c<n;++c){const double phase=pi*static_cast<double>((uint64_t(c)*c)%(2*n))/n;const Complex chirp(std::cos(phase),std::sin(phase));x[c]=a[c]*std::conj(chirp);y[c]=chirp;if(c)y[m-c]=chirp;}radix(x,false,work);radix(y,false,work);work.spend(m);for(std::size_t c=0;c<m;++c)x[c]*=y[c];radix(x,true,work);for(std::size_t c=0;c<n;++c){const double phase=pi*static_cast<double>((uint64_t(c)*c)%(2*n))/n;a[c]=x[c]*Complex(std::cos(phase),-std::sin(phase));}}
}
