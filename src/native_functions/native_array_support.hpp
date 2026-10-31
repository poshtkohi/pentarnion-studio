#pragma once
#include <string_view>
namespace penta::native_functions::detail {
inline constexpr std::string_view nativeArraySupport=R"ARRAY(
namespace penta_generated {
// Coefficients cross the existing real C ABI in row-major cell order, followed
// by the complete element coefficient order. MATLAB linear indexing is separate.
template<int Tag>constexpr unsigned fixed_array_width(){if constexpr(Tag>=0)return UniversalTraits<Tag>::dimension;else return Tag==-2?5:1;}
template<class S,unsigned R,unsigned C,int Tag>struct FixedArray {
    static constexpr unsigned rows=R,columns=C,components=fixed_array_width<Tag>();
    static constexpr int tag=Tag;
    std::array<S,R*C*components> c{};
};
template<class S,unsigned R,unsigned C,int Tag>auto array_element(const FixedArray<S,R,C,Tag>& a,unsigned index){
    if(index>=R*C)throw std::domain_error("Native array cell index exceeds its fixed shape.");
    if constexpr(Tag==-1)return a.c[index];
    else if constexpr(Tag==-2){Pent<S> value{};for(unsigned k=0;k<5;++k)value.c[k]=a.c[index*5+k];return value;}
    else{Universal<S,Tag> value{};for(unsigned k=0;k<value.c.size();++k)value.c[k]=a.c[index*value.c.size()+k];return value;}
}
template<class S,unsigned R,unsigned C,int Tag,class E>void array_put(FixedArray<S,R,C,Tag>& a,unsigned index,const E& value){
    if(index>=R*C)throw std::domain_error("Native array cell index exceeds its fixed shape.");
    if constexpr(Tag==-1)a.c[index]=value;
    else for(unsigned k=0;k<fixed_array_width<Tag>();++k)a.c[index*fixed_array_width<Tag>()+k]=value.c[k];
}
template<int Tag,class S>auto array_scalar(const S& value){if constexpr(Tag==-1)return value;else if constexpr(Tag==-2)return lift<S>(value);else return universal_lift<S,Tag>(value);}
template<class S,unsigned R,unsigned C,int Tag>FixedArray<S,R,C,Tag> array_load(const S* values){FixedArray<S,R,C,Tag> out{};std::copy_n(values,out.c.size(),out.c.begin());return out;}
template<class S,unsigned R,unsigned C,int Tag,class... E>FixedArray<S,R,C,Tag> array_make(const E&... values){
    static_assert(sizeof...(E)==R*C);FixedArray<S,R,C,Tag> out{};unsigned n=0;(array_put(out,n++,values),...);return out;
}
template<int Target,class S,unsigned R,unsigned C,int Source>auto array_cast(const FixedArray<S,R,C,Source>& a){
    FixedArray<S,R,C,Target> out{};
    for(unsigned n=0;n<R*C;++n){if constexpr(Target==Source)array_put(out,n,array_element(a,n));else {static_assert(Source==-1,"Only real entries embed into another native algebra.");array_put(out,n,array_scalar<Target>(array_element(a,n)));}}
    return out;
}
template<class S,unsigned R,unsigned C,int Tag>auto operator+(const FixedArray<S,R,C,Tag>& a){return a;}
template<class S,unsigned R,unsigned C,int Tag>auto operator-(const FixedArray<S,R,C,Tag>& a){auto out=a;for(auto& x:out.c)x=-x;return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator+(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b){auto out=a;for(unsigned k=0;k<out.c.size();++k)out.c[k]=a.c[k]+b.c[k];return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator-(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b){auto out=a;for(unsigned k=0;k<out.c.size();++k)out.c[k]=a.c[k]-b.c[k];return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator*(const FixedArray<S,R,C,Tag>& a,const S& b){auto out=a;for(auto& x:out.c)x=x*b;return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator*(const S& a,const FixedArray<S,R,C,Tag>& b){return b*a;}
template<class S,unsigned R,unsigned C,int Tag>auto array_divide(const FixedArray<S,R,C,Tag>& a,const S& b){auto out=a;for(auto& x:out.c)x=divide(x,b);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_add_scalar(const FixedArray<S,R,C,Tag>& a,const S& b,bool subtract=false){auto out=a;const auto scalar=array_scalar<Tag>(b);for(unsigned n=0;n<R*C;++n)array_put(out,n,subtract?array_element(a,n)-scalar:array_element(a,n)+scalar);return out;}
template<int Tag,class E>auto array_product(const E& a,const E& b,double gamma){if constexpr(Tag==-1)return a*b;else if constexpr(Tag==-2)return product(a,b,gamma);else return universal_product(a,b);}
template<class S,unsigned R,unsigned K,unsigned C,int Tag>auto array_matmul(const FixedArray<S,R,K,Tag>& a,const FixedArray<S,K,C,Tag>& b,double gamma){
    FixedArray<S,R,C,Tag> out{};for(unsigned row=0;row<R;++row)for(unsigned column=0;column<C;++column){auto sum=array_element(out,row*C+column);for(unsigned inner=0;inner<K;++inner)sum=sum+array_product<Tag>(array_element(a,row*K+inner),array_element(b,inner*C+column),gamma);array_put(out,row*C+column,sum);}return out;
}
template<class S,unsigned R,unsigned C,int Tag>auto array_entry_product(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b,double gamma){auto out=a;for(unsigned n=0;n<R*C;++n)array_put(out,n,array_product<Tag>(array_element(a,n),array_element(b,n),gamma));return out;}
template<class S,unsigned R,unsigned C,int Tag,class E>auto array_add_element(const FixedArray<S,R,C,Tag>& a,const E& b,bool subtract=false){auto out=a;for(unsigned n=0;n<R*C;++n)array_put(out,n,subtract?array_element(a,n)-b:array_element(a,n)+b);return out;}
template<class S,unsigned R,unsigned C,int Tag,class E>auto array_scale_element(const FixedArray<S,R,C,Tag>& a,const E& b,double gamma,bool left=false){auto out=a;for(unsigned n=0;n<R*C;++n)array_put(out,n,left?array_product<Tag>(b,array_element(a,n),gamma):array_product<Tag>(array_element(a,n),b,gamma));return out;}
template<class S,unsigned N,int Tag>auto array_power(const FixedArray<S,N,N,Tag>& a,unsigned exponent,double gamma){
    if(exponent>64)throw std::domain_error("Native matrix powers are bounded to0..64.");
    if(!exponent){FixedArray<S,N,N,Tag> out{};for(unsigned n=0;n<N;++n)array_put(out,n*N+n,array_scalar<Tag>(S(1)));return out;}
    auto out=a;for(unsigned n=1;n<exponent;++n)out=array_matmul(out,a,gamma);return out;
}
template<class S,unsigned R,unsigned C,int Tag>auto array_entry_divide(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,-1>& b){auto out=a;for(unsigned n=0;n<R*C;++n){const auto denominator=array_element(b,n);for(unsigned k=0;k<out.components;++k)out.c[n*out.components+k]=divide(a.c[n*out.components+k],denominator);}return out;}
template<class E>auto array_conjugate(const E& value){return value;}
template<class S>auto array_conjugate(const Pent<S>& value){return conjugate(value);}
template<class S,unsigned Tag>auto array_conjugate(const Universal<S,Tag>& value){return universal_conjugate(value);}
template<class S,unsigned R,unsigned C,int Tag>auto array_transpose(const FixedArray<S,R,C,Tag>& a,bool conjugate=false){FixedArray<S,C,R,Tag> out{};for(unsigned row=0;row<R;++row)for(unsigned column=0;column<C;++column){const auto value=array_element(a,row*C+column);array_put(out,column*R+row,conjugate?array_conjugate(value):value);}return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_at(const FixedArray<S,R,C,Tag>& a,unsigned row,unsigned column){if(row>=R||column>=C)throw std::domain_error("Native array index exceeds its fixed shape.");return array_element(a,row*C+column);}
template<class S,unsigned R,unsigned C,int Tag>auto array_linear_at(const FixedArray<S,R,C,Tag>& a,unsigned oneBased){if(oneBased<1||oneBased>R*C)throw std::domain_error("Native array linear index exceeds its fixed shape.");const unsigned n=oneBased-1;return array_at(a,n%R,n/R);}
template<int Target,class S,unsigned R,unsigned C,int Tag,class Function>auto array_map_as(const FixedArray<S,R,C,Tag>& a,Function function){FixedArray<S,R,C,Target> out{};for(unsigned n=0;n<R*C;++n)array_put(out,n,function(array_element(a,n)));return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_sum(const FixedArray<S,R,C,Tag>& a){auto zero=a;for(auto& x:zero.c)x=S(0);auto sum=array_element(zero,0);for(unsigned n=0;n<R*C;++n)sum=sum+array_element(a,n);return sum;}
template<unsigned Axis,class S,unsigned R,unsigned C,int Tag>auto array_reduce(const FixedArray<S,R,C,Tag>& a,bool mean=false){
    if constexpr(Axis==1){FixedArray<S,1,C,Tag> out{};for(unsigned column=0;column<C;++column){auto sum=array_element(out,column);for(unsigned row=0;row<R;++row)sum=sum+array_element(a,row*C+column);array_put(out,column,sum);}return mean?array_divide(out,S(R)):out;}
    else if constexpr(Axis==2){FixedArray<S,R,1,Tag> out{};for(unsigned row=0;row<R;++row){auto sum=array_element(out,row);for(unsigned column=0;column<C;++column)sum=sum+array_element(a,row*C+column);array_put(out,row,sum);}return mean?array_divide(out,S(C)):out;}
    else return a;
}
template<class S,unsigned R,unsigned C,int Tag>auto array_norm(const FixedArray<S,R,C,Tag>& a){S sum(0);for(const auto& x:a.c)sum=sum+x*x;return cb_sqrt(sum);}
template<class S,unsigned R,unsigned C,int Tag>bool array_equal(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b){for(unsigned k=0;k<a.c.size();++k)if(!(a.c[k]==b.c[k]))return false;return true;}
}
)ARRAY";
}
