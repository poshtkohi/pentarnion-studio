#pragma once
#include <string_view>
namespace penta::native_functions::detail {
inline constexpr std::string_view nativeTensorSupport=R"TENSOR(
namespace penta_generated {
// Tensor elements use MATLAB column-major ordering across every axis. Every
// element retains its adjacent complete algebra coefficients. FixedArray's
// row-major ABI and all existing generated sources remain unchanged.
template<class S,int Tag,unsigned... D>struct TypedTensor {
    static constexpr unsigned rank=sizeof...(D),elements=(1u*...*D),components=fixed_array_width<Tag>();
    static constexpr int tag=Tag;
    static constexpr std::array<unsigned,rank> shape{{D...}};
    std::array<S,elements*components> c{};
};
template<class S,int Tag,unsigned... D>auto tensor_element(const TypedTensor<S,Tag,D...>& a,unsigned index){
    if(index>=a.elements)throw std::domain_error("Native tensor index exceeds its immutable shape.");
    if constexpr(Tag==-1)return a.c[index];
    else if constexpr(Tag==-2){Pent<S> value{};for(unsigned k=0;k<5;++k)value.c[k]=a.c[index*5+k];return value;}
    else{Universal<S,Tag> value{};for(unsigned k=0;k<value.c.size();++k)value.c[k]=a.c[index*value.c.size()+k];return value;}
}
template<class S,int Tag,unsigned... D,class E>void tensor_put(TypedTensor<S,Tag,D...>& a,unsigned index,const E& value){
    if(index>=a.elements)throw std::domain_error("Native tensor assignment exceeds its immutable shape.");
    if constexpr(Tag==-1)a.c[index]=value;
    else for(unsigned k=0;k<a.components;++k)a.c[index*a.components+k]=value.c[k];
}
template<class S,int Tag,unsigned... D>auto tensor_load(const S* values){TypedTensor<S,Tag,D...> out{};std::copy_n(values,out.c.size(),out.c.begin());return out;}
template<class S,int Tag,unsigned... D,class E>auto tensor_fill(const E& value){TypedTensor<S,Tag,D...> out{};for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,value);return out;}
template<class S,int Tag,unsigned... D,unsigned R,unsigned C,int Source>auto tensor_from_array(const FixedArray<S,R,C,Source>& a){
    TypedTensor<S,Tag,D...> out{};static_assert(R*C==out.elements||R*C==1,"Tensor reshape must retain element count.");
    static_assert(Tag==Source,"Tensor inference cannot change the complete entry algebra.");
    for(unsigned n=0;n<out.elements;++n){const auto cell=R*C==1?0:(n%R)*C+n/R;tensor_put(out,n,array_element(a,cell));}return out;
}
template<class S,int Tag,unsigned... D,unsigned R,unsigned C>auto tensor_from_coordinates(const FixedArray<S,R,C,-1>& a){
    TypedTensor<S,Tag,D...> out{};static_assert(C==out.components&&(R==1||R==out.elements),"Tensor coordinate rows must retain every coefficient.");
    for(unsigned n=0;n<out.elements;++n)for(unsigned k=0;k<out.components;++k)out.c[n*out.components+k]=a.c[(R==1?0:n)*C+k];return out;
}
template<unsigned... E,class S,int Tag,unsigned... D>auto tensor_reshape(const TypedTensor<S,Tag,D...>& a){
    TypedTensor<S,Tag,E...> out{};static_assert(out.elements==a.elements,"Tensor reshape must retain element count.");std::copy(a.c.begin(),a.c.end(),out.c.begin());return out;
}
template<class S,int Tag,unsigned... D>auto operator+(const TypedTensor<S,Tag,D...>& a){return a;}
template<class S,int Tag,unsigned... D>auto operator-(const TypedTensor<S,Tag,D...>& a){auto out=a;for(auto& x:out.c)x=-x;return out;}
template<class S,int Tag,unsigned... D>auto operator+(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b){auto out=a;for(unsigned n=0;n<out.c.size();++n)out.c[n]=a.c[n]+b.c[n];return out;}
template<class S,int Tag,unsigned... D>auto operator-(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b){auto out=a;for(unsigned n=0;n<out.c.size();++n)out.c[n]=a.c[n]-b.c[n];return out;}
template<class S,int Tag,unsigned... D,class E>auto tensor_add_element(const TypedTensor<S,Tag,D...>& a,const E& b,bool subtract=false){auto out=a;for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,subtract?tensor_element(a,n)-b:tensor_element(a,n)+b);return out;}
template<class S,int Tag,unsigned... D>auto tensor_entry_product(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b,double gamma){auto out=a;for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,array_product<Tag>(tensor_element(a,n),tensor_element(b,n),gamma));return out;}
template<class S,int Tag,unsigned... D>auto tensor_scale_real(const TypedTensor<S,Tag,D...>& a,const S& b){auto out=a;for(auto& value:out.c)value=value*b;return out;}
template<class S,int Tag,unsigned... D,class E>auto tensor_scale_element(const TypedTensor<S,Tag,D...>& a,const E& b,double gamma,bool left=false){auto out=a;for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,left?array_product<Tag>(b,tensor_element(a,n),gamma):array_product<Tag>(tensor_element(a,n),b,gamma));return out;}
template<class S,int Tag,unsigned... D>auto tensor_divide_real(const TypedTensor<S,Tag,D...>& a,const S& b){auto out=a;for(auto& value:out.c)value=divide(value,b);return out;}
template<int Target,class S,int Tag,unsigned... D,class F>auto tensor_map_as(const TypedTensor<S,Tag,D...>& a,F function){TypedTensor<S,Target,D...> out{};for(unsigned n=0;n<out.elements;++n)tensor_put(out,n,function(tensor_element(a,n)));return out;}
template<class S,int Tag,unsigned... D>auto tensor_coefficient(const TypedTensor<S,Tag,D...>& a,unsigned index){if(index>=a.components)throw std::domain_error("Native tensor coefficient exceeds its entry algebra.");TypedTensor<S,-1,D...> out{};for(unsigned n=0;n<out.elements;++n)out.c[n]=a.c[n*a.components+index];return out;}
template<class S,int Tag,unsigned... D>auto tensor_coordinates(const TypedTensor<S,Tag,D...>& a){using T=TypedTensor<S,Tag,D...>;FixedArray<S,T::elements,T::components,-1> out{};std::copy(a.c.begin(),a.c.end(),out.c.begin());return out;}
template<class S,int Tag,unsigned... D>auto tensor_norm(const TypedTensor<S,Tag,D...>& a){S sum(0);for(const auto& value:a.c)sum=sum+value*value;return cb_sqrt(sum);}
template<class S,int Tag,unsigned... D>bool tensor_equal(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b){for(unsigned n=0;n<a.c.size();++n)if(!(a.c[n]==b.c[n]))return false;return true;}
}
)TENSOR";
}
