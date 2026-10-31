#pragma once
#include <string_view>
namespace penta::native_functions::detail {
inline constexpr std::string_view distributionModelSupport=R"PENTA_BRIDGE(// Model-specific type and ABI forwarding glue. Numerical kernels are in the compiled bridge.
#include <pentarnion/native_dense.hpp>
#include <type_traits>
namespace penta_generated {
using UniversalTerm=BridgeTerm;
template<unsigned Tag>struct UniversalTraits;
template<class S,unsigned Tag>struct Universal {std::array<S,UniversalTraits<Tag>::dimension> c{};};
template<int Tag>constexpr unsigned fixed_array_width(){if constexpr(Tag>=0)return UniversalTraits<Tag>::dimension;else return Tag==-2?5:1;}
template<class S,unsigned R,unsigned C,int Tag>struct FixedArray {static constexpr unsigned rows=R,columns=C,components=fixed_array_width<Tag>();static constexpr int tag=Tag;std::array<S,R*C*components> c{};};
template<class S,int Tag,unsigned...D>struct TypedTensor {static constexpr unsigned rank=sizeof...(D),elements=(1u*...*D),components=fixed_array_width<Tag>();static constexpr int tag=Tag;static constexpr std::array<unsigned,rank> shape{{D...}};std::array<S,elements*components> c{};};
template<unsigned Tag>const BridgeDefinition& bridge_definition(){using T=UniversalTraits<Tag>;static const BridgeDefinition definition{T::dimension,T::has_unit,T::has_conjugation,T::has_metric,T::scalar_first,T::canonical_reciprocal,T::quadratic,T::associative_exact,T::power_associative_exact,T::unit,T::conjugation,T::metric,T::pure_square,T::terms,T::product_bound};return definition;}
template<int Tag>BridgeEntry bridge_entry(){if constexpr(Tag>=0)return {Tag,&bridge_definition<Tag>()};else return {Tag,nullptr};}
template<class S>S* bridge_data(S& value){return &value;}template<class S>const S* bridge_data(const S& value){return &value;}
template<class S>S* bridge_data(Pent<S>& value){return value.c.data();}template<class S>const S* bridge_data(const Pent<S>& value){return value.c.data();}
template<class S,unsigned T>S* bridge_data(Universal<S,T>& value){return value.c.data();}template<class S,unsigned T>const S* bridge_data(const Universal<S,T>& value){return value.c.data();}
template<class S,int Tag>using BridgeCell=std::conditional_t<Tag==-1,S,std::conditional_t<Tag==-2,Pent<S>,Universal<S,static_cast<unsigned>(Tag)>>>;
template<class S,unsigned T>Universal<S,T> universal_load(const S* values){Universal<S,T> out{};bridge_copy(out.c.data(),values,out.c.size());return out;}
template<class S,unsigned T>Universal<S,T> universal_basis(unsigned index){Universal<S,T> out{};bridge_cell<S>(out.c.data(),nullptr,nullptr,bridge_entry<T>(),BridgeOp::Basis,index,0);return out;}
template<class S,unsigned T>Universal<S,T> universal_lift(const S& value){Universal<S,T> out{};bridge_cell<S>(out.c.data(),&value,nullptr,bridge_entry<T>(),BridgeOp::Lift,0,0);return out;}
template<class S,unsigned T>Universal<S,T> operator+(const Universal<S,T>& a,const Universal<S,T>& b){Universal<S,T> out{};bridge_cell(out.c.data(),a.c.data(),b.c.data(),bridge_entry<T>(),BridgeOp::Add,0,0);return out;}
template<class S,unsigned T>Universal<S,T> operator-(const Universal<S,T>& a,const Universal<S,T>& b){Universal<S,T> out{};bridge_cell(out.c.data(),a.c.data(),b.c.data(),bridge_entry<T>(),BridgeOp::Subtract,0,0);return out;}
template<class S,unsigned T>Universal<S,T> operator-(const Universal<S,T>& a){Universal<S,T> out{};bridge_cell<S>(out.c.data(),a.c.data(),nullptr,bridge_entry<T>(),BridgeOp::Negate,0,0);return out;}
template<class S,unsigned T>Universal<S,T> operator+(const Universal<S,T>& a){return a;}
template<class S,unsigned T>Universal<S,T> operator*(const Universal<S,T>& a,const S& b){Universal<S,T> out{};bridge_cell(out.c.data(),a.c.data(),&b,bridge_entry<T>(),BridgeOp::Scale,0,0);return out;}
template<class S,unsigned T>Universal<S,T> operator*(const S& a,const Universal<S,T>& b){return b*a;}
template<class S,unsigned T>Universal<S,T> divide(const Universal<S,T>& a,const S& b){Universal<S,T> out{};bridge_cell(out.c.data(),a.c.data(),&b,bridge_entry<T>(),BridgeOp::Divide,0,0);return out;}
template<class S,unsigned T>Universal<S,T> universal_product(const Universal<S,T>& a,const Universal<S,T>& b){Universal<S,T> out{};bridge_cell(out.c.data(),a.c.data(),b.c.data(),bridge_entry<T>(),BridgeOp::Product,0,0);return out;}
template<class S,unsigned T>Universal<S,T> universal_conjugate(const Universal<S,T>& a){Universal<S,T> out{};bridge_cell<S>(out.c.data(),a.c.data(),nullptr,bridge_entry<T>(),BridgeOp::Conjugate,0,0);return out;}
template<class S,unsigned T>Universal<S,T> universal_pure(const Universal<S,T>& a){Universal<S,T> out{};bridge_cell<S>(out.c.data(),a.c.data(),nullptr,bridge_entry<T>(),BridgeOp::Pure,0,0);return out;}
template<class S,unsigned T>S universal_coordinate_norm2(const Universal<S,T>& a){S out{};bridge_cell<S>(&out,a.c.data(),nullptr,bridge_entry<T>(),BridgeOp::CoordinateNorm2,0,0);return out;}
template<class S,unsigned T>S universal_norm2(const Universal<S,T>& a){S out{};bridge_cell<S>(&out,a.c.data(),nullptr,bridge_entry<T>(),BridgeOp::Norm2,0,0);return out;}
template<class S,unsigned T>bool universal_equal(const Universal<S,T>& a,const Universal<S,T>& b){return bridge_equal(a.c.data(),b.c.data(),a.c.size());}
template<class S,unsigned T>Universal<S,T> universal_inverse(const Universal<S,T>& a){Universal<S,T> out{};bridge_cell<S>(out.c.data(),a.c.data(),nullptr,bridge_entry<T>(),BridgeOp::Inverse,0,0);return out;}
template<class S,unsigned T>Universal<S,T> universal_power(const Universal<S,T>& a,long long exponent){Universal<S,T> out{};bridge_cell<S>(out.c.data(),a.c.data(),nullptr,bridge_entry<T>(),BridgeOp::Power,exponent,0);return out;}
template<class S,unsigned T>Universal<S,T> universal_whole(const Universal<S,T>& a,unsigned operation){Universal<S,T> out{};bridge_cell<S>(out.c.data(),a.c.data(),nullptr,bridge_entry<T>(),BridgeOp::Whole,operation,0);return out;}
template<class S,unsigned T>Universal<S,T> universal_complex_log(const Universal<S,T>& a){Universal<S,T> out{};bridge_cell<S>(out.c.data(),a.c.data(),nullptr,bridge_entry<T>(),BridgeOp::ComplexLog,0,0);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_element(const FixedArray<S,R,C,Tag>& a,unsigned index){BridgeCell<S,Tag> out{};bridge_load_index(bridge_data(out),a.c.data(),index,R*C,a.components);return out;}
template<class S,unsigned R,unsigned C,int Tag,class E>void array_put(FixedArray<S,R,C,Tag>& a,unsigned index,const E& value){bridge_store_index(a.c.data(),bridge_data(value),index,R*C,a.components);}
template<int Tag,class S>auto array_scalar(const S& value){if constexpr(Tag==-1)return value;else if constexpr(Tag==-2)return lift<S>(value);else return universal_lift<S,Tag>(value);}
template<class S,unsigned R,unsigned C,int Tag>FixedArray<S,R,C,Tag> array_load(const S* values){FixedArray<S,R,C,Tag> out{};bridge_copy(out.c.data(),values,out.c.size());return out;}
template<class S,unsigned R,unsigned C,int Tag,class...E>FixedArray<S,R,C,Tag> array_make(const E&...values){static_assert(sizeof...(E)==R*C);FixedArray<S,R,C,Tag> out{};unsigned n=0;(array_put(out,n++,values),...);return out;}
template<int Target,class S,unsigned R,unsigned C,int Source>auto array_cast(const FixedArray<S,R,C,Source>& a){FixedArray<S,R,C,Target> out{};if constexpr(Target==Source)bridge_copy(out.c.data(),a.c.data(),out.c.size());else{static_assert(Source==-1);bridge_map<S>(out.c.data(),a.c.data(),R*C,a.components,out.components,[](S* destination,const S* input,void*){const auto value=array_scalar<Target>(*input);bridge_copy(destination,bridge_data(value),fixed_array_width<Target>());},nullptr);}return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator+(const FixedArray<S,R,C,Tag>& a){return a;}
template<class S,unsigned R,unsigned C,int Tag>auto operator-(const FixedArray<S,R,C,Tag>& a){auto out=a;bridge_cells<S>(out.c.data(),a.c.data(),nullptr,R*C,bridge_entry<Tag>(),BridgeOp::Negate,0,0,false,false);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator+(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b){auto out=a;bridge_cells(out.c.data(),a.c.data(),b.c.data(),R*C,bridge_entry<Tag>(),BridgeOp::Add,0,0,false,false);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator-(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b){auto out=a;bridge_cells(out.c.data(),a.c.data(),b.c.data(),R*C,bridge_entry<Tag>(),BridgeOp::Subtract,0,0,false,false);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator*(const FixedArray<S,R,C,Tag>& a,const S& b){auto out=a;bridge_cells(out.c.data(),a.c.data(),&b,R*C,bridge_entry<Tag>(),BridgeOp::Scale,0,0,true,false);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto operator*(const S& a,const FixedArray<S,R,C,Tag>& b){return b*a;}
template<class S,unsigned R,unsigned C,int Tag>auto array_divide(const FixedArray<S,R,C,Tag>& a,const S& b){auto out=a;bridge_cells(out.c.data(),a.c.data(),&b,R*C,bridge_entry<Tag>(),BridgeOp::Divide,0,0,true,false);return out;}
template<class S,unsigned R,unsigned C,int Tag,class E>auto array_add_element(const FixedArray<S,R,C,Tag>& a,const E& b,bool subtract=false){auto out=a;bridge_cells(out.c.data(),a.c.data(),bridge_data(b),R*C,bridge_entry<Tag>(),subtract?BridgeOp::Subtract:BridgeOp::Add,0,0,true,false);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_add_scalar(const FixedArray<S,R,C,Tag>& a,const S& b,bool subtract=false){return array_add_element(a,array_scalar<Tag>(b),subtract);}
template<int Tag,class E>auto array_product(const E& a,const E& b,double gamma){if constexpr(Tag==-1)return a*b;else if constexpr(Tag==-2)return product(a,b,gamma);else return universal_product(a,b);}
template<class S,unsigned R,unsigned K,unsigned C,int Tag>auto array_matmul(const FixedArray<S,R,K,Tag>& a,const FixedArray<S,K,C,Tag>& b,double gamma){FixedArray<S,R,C,Tag> out{};bridge_matmul(out.c.data(),a.c.data(),b.c.data(),R,K,C,bridge_entry<Tag>(),gamma);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_entry_product(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b,double gamma){auto out=a;bridge_cells(out.c.data(),a.c.data(),b.c.data(),R*C,bridge_entry<Tag>(),BridgeOp::Product,0,gamma,false,false);return out;}
template<class S,unsigned R,unsigned C,int Tag,class E>auto array_scale_element(const FixedArray<S,R,C,Tag>& a,const E& b,double gamma,bool left=false){auto out=a;bridge_cells(out.c.data(),a.c.data(),bridge_data(b),R*C,bridge_entry<Tag>(),BridgeOp::Product,0,gamma,true,left);return out;}
template<class S,unsigned N,int Tag>auto array_power(const FixedArray<S,N,N,Tag>& a,unsigned exponent,double gamma){auto out=a;bridge_matrix_power(out.c.data(),a.c.data(),N,bridge_entry<Tag>(),exponent,gamma);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_entry_divide(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,-1>& b){auto out=a;bridge_entry_divide(out.c.data(),a.c.data(),b.c.data(),R*C,a.components);return out;}
template<class E>auto array_conjugate(const E& value){return value;}template<class S>auto array_conjugate(const Pent<S>& value){return conjugate(value);}template<class S,unsigned T>auto array_conjugate(const Universal<S,T>& value){return universal_conjugate(value);}
template<class S,unsigned R,unsigned C,int Tag>auto array_transpose(const FixedArray<S,R,C,Tag>& a,bool conjugation=false){FixedArray<S,C,R,Tag> out{};bridge_transpose(out.c.data(),a.c.data(),R,C,bridge_entry<Tag>(),conjugation);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_at(const FixedArray<S,R,C,Tag>& a,unsigned row,unsigned column){return array_element(a,bridge_array_index(R,C,row,column));}
template<class S,unsigned R,unsigned C,int Tag>auto array_linear_at(const FixedArray<S,R,C,Tag>& a,unsigned oneBased){return array_element(a,bridge_linear_index(R,C,oneBased));}
template<int Target,class S,unsigned R,unsigned C,int Tag,class F>auto array_map_as(const FixedArray<S,R,C,Tag>& a,F function){FixedArray<S,R,C,Target> out{};bridge_map<S>(out.c.data(),a.c.data(),R*C,a.components,out.components,[](S* destination,const S* input,void* context){BridgeCell<S,Tag> cell{};bridge_copy(bridge_data(cell),input,fixed_array_width<Tag>());const auto value=(*static_cast<F*>(context))(cell);bridge_copy(destination,bridge_data(value),fixed_array_width<Target>());},&function);return out;}
template<class S,unsigned R,unsigned C,int Tag>auto array_sum(const FixedArray<S,R,C,Tag>& a){BridgeCell<S,Tag> out{};bridge_reduce(bridge_data(out),a.c.data(),R,C,bridge_entry<Tag>(),0,false);return out;}
template<unsigned Axis,class S,unsigned R,unsigned C,int Tag>auto array_reduce(const FixedArray<S,R,C,Tag>& a,bool mean=false){if constexpr(Axis==1){FixedArray<S,1,C,Tag> out{};bridge_reduce(out.c.data(),a.c.data(),R,C,bridge_entry<Tag>(),Axis,mean);return out;}else if constexpr(Axis==2){FixedArray<S,R,1,Tag> out{};bridge_reduce(out.c.data(),a.c.data(),R,C,bridge_entry<Tag>(),Axis,mean);return out;}else return a;}
template<class S,unsigned R,unsigned C,int Tag>auto array_norm(const FixedArray<S,R,C,Tag>& a){return bridge_norm(a.c.data(),a.c.size());}
template<class S,unsigned R,unsigned C,int Tag>bool array_equal(const FixedArray<S,R,C,Tag>& a,const FixedArray<S,R,C,Tag>& b){return bridge_equal(a.c.data(),b.c.data(),a.c.size());}
}

namespace penta_generated {
template<class S,int Tag,unsigned...D>auto tensor_element(const TypedTensor<S,Tag,D...>& a,unsigned index){BridgeCell<S,Tag> out{};bridge_load_index(bridge_data(out),a.c.data(),index,a.elements,a.components);return out;}
template<class S,int Tag,unsigned...D,class E>void tensor_put(TypedTensor<S,Tag,D...>& a,unsigned index,const E& value){bridge_store_index(a.c.data(),bridge_data(value),index,a.elements,a.components);}
template<class S,int Tag,unsigned...D>auto tensor_load(const S* values){TypedTensor<S,Tag,D...> out{};bridge_copy(out.c.data(),values,out.c.size());return out;}
template<class S,int Tag,unsigned...D,class E>auto tensor_fill(const E& value){TypedTensor<S,Tag,D...> out{};bridge_fill(out.c.data(),bridge_data(value),out.elements,out.components);return out;}
template<class S,int Tag,unsigned...D,unsigned R,unsigned C,int Source>auto tensor_from_array(const FixedArray<S,R,C,Source>& a){TypedTensor<S,Tag,D...> out{};static_assert(R*C==out.elements||R*C==1);static_assert(Tag==Source);bridge_tensor_from_array(out.c.data(),a.c.data(),R,C,out.components,out.elements);return out;}
template<class S,int Tag,unsigned...D,unsigned R,unsigned C>auto tensor_from_coordinates(const FixedArray<S,R,C,-1>& a){TypedTensor<S,Tag,D...> out{};static_assert(C==out.components&&(R==1||R==out.elements));if constexpr(R==1)bridge_fill(out.c.data(),a.c.data(),out.elements,out.components);else bridge_copy(out.c.data(),a.c.data(),out.c.size());return out;}
template<unsigned...E,class S,int Tag,unsigned...D>auto tensor_reshape(const TypedTensor<S,Tag,D...>& a){TypedTensor<S,Tag,E...> out{};static_assert(out.elements==a.elements);bridge_copy(out.c.data(),a.c.data(),out.c.size());return out;}
template<class S,int Tag,unsigned...D>auto operator+(const TypedTensor<S,Tag,D...>& a){return a;}
template<class S,int Tag,unsigned...D>auto operator-(const TypedTensor<S,Tag,D...>& a){auto out=a;bridge_cells<S>(out.c.data(),a.c.data(),nullptr,a.elements,bridge_entry<Tag>(),BridgeOp::Negate,0,0,false,false);return out;}
template<class S,int Tag,unsigned...D>auto operator+(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b){auto out=a;bridge_cells(out.c.data(),a.c.data(),b.c.data(),a.elements,bridge_entry<Tag>(),BridgeOp::Add,0,0,false,false);return out;}
template<class S,int Tag,unsigned...D>auto operator-(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b){auto out=a;bridge_cells(out.c.data(),a.c.data(),b.c.data(),a.elements,bridge_entry<Tag>(),BridgeOp::Subtract,0,0,false,false);return out;}
template<class S,int Tag,unsigned...D,class E>auto tensor_add_element(const TypedTensor<S,Tag,D...>& a,const E& b,bool subtract=false){auto out=a;bridge_cells(out.c.data(),a.c.data(),bridge_data(b),a.elements,bridge_entry<Tag>(),subtract?BridgeOp::Subtract:BridgeOp::Add,0,0,true,false);return out;}
template<class S,int Tag,unsigned...D>auto tensor_entry_product(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b,double gamma){auto out=a;bridge_cells(out.c.data(),a.c.data(),b.c.data(),a.elements,bridge_entry<Tag>(),BridgeOp::Product,0,gamma,false,false);return out;}
template<class S,int Tag,unsigned...D>auto tensor_scale_real(const TypedTensor<S,Tag,D...>& a,const S& b){auto out=a;bridge_cells(out.c.data(),a.c.data(),&b,a.elements,bridge_entry<Tag>(),BridgeOp::Scale,0,0,true,false);return out;}
template<class S,int Tag,unsigned...D,class E>auto tensor_scale_element(const TypedTensor<S,Tag,D...>& a,const E& b,double gamma,bool left=false){auto out=a;bridge_cells(out.c.data(),a.c.data(),bridge_data(b),a.elements,bridge_entry<Tag>(),BridgeOp::Product,0,gamma,true,left);return out;}
template<class S,int Tag,unsigned...D>auto tensor_divide_real(const TypedTensor<S,Tag,D...>& a,const S& b){auto out=a;bridge_cells(out.c.data(),a.c.data(),&b,a.elements,bridge_entry<Tag>(),BridgeOp::Divide,0,0,true,false);return out;}
template<int Target,class S,int Tag,unsigned...D,class F>auto tensor_map_as(const TypedTensor<S,Tag,D...>& a,F function){TypedTensor<S,Target,D...> out{};bridge_map<S>(out.c.data(),a.c.data(),a.elements,a.components,out.components,[](S* destination,const S* input,void* context){BridgeCell<S,Tag> cell{};bridge_copy(bridge_data(cell),input,fixed_array_width<Tag>());const auto value=(*static_cast<F*>(context))(cell);bridge_copy(destination,bridge_data(value),fixed_array_width<Target>());},&function);return out;}
template<class S,int Tag,unsigned...D>auto tensor_coefficient(const TypedTensor<S,Tag,D...>& a,unsigned index){TypedTensor<S,-1,D...> out{};bridge_coefficient(out.c.data(),a.c.data(),a.elements,a.components,index);return out;}
template<class S,int Tag,unsigned...D>auto tensor_coordinates(const TypedTensor<S,Tag,D...>& a){using T=TypedTensor<S,Tag,D...>;FixedArray<S,T::elements,T::components,-1> out{};bridge_copy(out.c.data(),a.c.data(),out.c.size());return out;}
template<class S,int Tag,unsigned...D>auto tensor_norm(const TypedTensor<S,Tag,D...>& a){return bridge_norm(a.c.data(),a.c.size());}
template<class S,int Tag,unsigned...D>bool tensor_equal(const TypedTensor<S,Tag,D...>& a,const TypedTensor<S,Tag,D...>& b){return bridge_equal(a.c.data(),b.c.data(),a.c.size());}
}
)PENTA_BRIDGE";
}
